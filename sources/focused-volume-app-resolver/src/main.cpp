#include <KApplicationTrader>
#include <KService>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <QSet>
#include <QTextStream>

#include <limits>

namespace
{
struct Identity {
    QString desktopFile;
    QString resourceClass;
    QString resourceName;
    QString processExecutable;
    QString processComm;
    QString processCommand;
};

struct Match {
    int score = 0;
    int strongMatches = 0;
};

QString withoutDesktopSuffix(QString value)
{
    value = value.trimmed();
    if (value.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive)) {
        value.chop(8);
    }
    return value;
}

bool sameIdentity(const QString &left, const QString &right)
{
    return !left.isEmpty() && !right.isEmpty() && left.compare(right, Qt::CaseInsensitive) == 0;
}

QString executableFromCommand(const QString &command)
{
    QStringList parts = QProcess::splitCommand(command);
    while (!parts.isEmpty() && parts.constFirst().startsWith(QLatin1Char('%'))) {
        parts.removeFirst();
    }
    if (parts.isEmpty()) {
        return {};
    }

    QString executable = QFileInfo(parts.takeFirst()).fileName();
    if (executable == QLatin1String("env")) {
        while (!parts.isEmpty()) {
            const QString part = parts.takeFirst();
            if (part.startsWith(QLatin1Char('-')) || part.contains(QLatin1Char('='))) {
                continue;
            }
            if (!part.startsWith(QLatin1Char('%'))) {
                executable = QFileInfo(part).fileName();
                break;
            }
        }
    }
    return executable;
}

QString readTrimmedFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromLocal8Bit(file.readAll()).trimmed();
}

QString readFirstCommandArgument(qint64 pid)
{
    QFile file(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray data = file.readAll();
    const qsizetype end = data.indexOf('\0');
    const QByteArray first = end >= 0 ? data.first(end) : data;
    return QFileInfo(QString::fromLocal8Bit(first)).fileName();
}

Identity collectIdentity(const QString &desktopFile, qint64 pid, const QString &resourceClass, const QString &resourceName)
{
    Identity identity;
    identity.desktopFile = desktopFile.trimmed();
    identity.resourceClass = resourceClass.trimmed();
    identity.resourceName = resourceName.trimmed();

    if (pid > 1) {
        const QString exePath = QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).symLinkTarget();
        identity.processExecutable = QFileInfo(exePath).fileName();
        identity.processComm = readTrimmedFile(QStringLiteral("/proc/%1/comm").arg(pid));
        identity.processCommand = readFirstCommandArgument(pid);
    }
    return identity;
}

QStringList candidateIds(const KService::Ptr &service)
{
    return {
        withoutDesktopSuffix(service->desktopEntryName()),
        withoutDesktopSuffix(service->storageId()),
        withoutDesktopSuffix(QFileInfo(service->entryPath()).fileName()),
    };
}

Match scoreService(const KService::Ptr &service, const Identity &identity)
{
    Match result;
    if (!service || !service->isValid() || !service->isApplication()) {
        return result;
    }

    const QString desktopToken = withoutDesktopSuffix(identity.desktopFile);
    const QString startupWMClass = service->property<QString>(QStringLiteral("StartupWMClass"));
    const QString candidateExecutable = executableFromCommand(service->exec());
    const QStringList ids = candidateIds(service);

    const auto anyIdMatches = [&ids](const QString &value) {
        for (const QString &id : ids) {
            if (sameIdentity(value, id)) {
                return true;
            }
        }
        return false;
    };

    if (anyIdMatches(desktopToken)) {
        result.score += 100;
        ++result.strongMatches;
    }

    if (sameIdentity(identity.processExecutable, candidateExecutable)
        || sameIdentity(identity.processCommand, candidateExecutable)) {
        result.score += 80;
        ++result.strongMatches;
    }

    if (sameIdentity(identity.resourceClass, startupWMClass)) {
        result.score += 70;
        ++result.strongMatches;
    }

    if (anyIdMatches(identity.resourceClass) || anyIdMatches(identity.resourceName)) {
        result.score += 60;
        ++result.strongMatches;
    }

    if ((sameIdentity(identity.processComm, candidateExecutable) || anyIdMatches(identity.processComm))) {
        result.score += 50;
        ++result.strongMatches;
    }

    if (sameIdentity(identity.resourceClass, candidateExecutable)
        || sameIdentity(identity.resourceName, candidateExecutable)) {
        result.score += 40;
        ++result.strongMatches;
    }

    if (sameIdentity(identity.resourceClass, service->name())
        || sameIdentity(identity.resourceClass, service->untranslatedName())
        || sameIdentity(identity.resourceName, service->name())
        || sameIdentity(identity.resourceName, service->untranslatedName())) {
        result.score += 20;
    }

    return result;
}

KService::Ptr directService(const QString &desktopFile)
{
    const QString value = desktopFile.trimmed();
    if (value.isEmpty()) {
        return {};
    }

    QList<KService::Ptr> candidates;
    if (QFileInfo(value).isAbsolute()) {
        candidates << KService::serviceByDesktopPath(value) << KService::serviceByStorageId(value);
    } else {
        candidates << KService::serviceByStorageId(value);
        if (!value.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive)) {
            candidates << KService::serviceByStorageId(value + QStringLiteral(".desktop"));
        }
        candidates << KService::serviceByDesktopName(withoutDesktopSuffix(value));
    }

    for (const KService::Ptr &candidate : std::as_const(candidates)) {
        if (candidate && candidate->isValid() && candidate->isApplication()) {
            return candidate;
        }
    }
    return {};
}

KService::Ptr followAliases(KService::Ptr service)
{
    QSet<QString> seen;
    for (int depth = 0; service && depth < 8; ++depth) {
        const QString storageId = service->storageId();
        if (seen.contains(storageId)) {
            break;
        }
        seen.insert(storageId);

        const QString alias = service->aliasFor().trimmed();
        if (alias.isEmpty()) {
            break;
        }

        KService::Ptr target = KService::serviceByStorageId(alias);
        if (!target) {
            target = KService::serviceByDesktopName(withoutDesktopSuffix(alias));
        }
        if (!target || !target->isValid() || !target->isApplication()) {
            break;
        }
        service = target;
    }
    return service;
}

KService::Ptr tradedService(const Identity &identity)
{
    const KService::List candidates = KApplicationTrader::query([&identity](const KService::Ptr &service) {
        const Match match = scoreService(service, identity);
        return match.strongMatches > 0;
    });

    KService::Ptr best;
    Match bestMatch;
    bool ambiguous = false;

    for (const KService::Ptr &candidate : candidates) {
        const Match match = scoreService(candidate, identity);
        if (match.score > bestMatch.score
            || (match.score == bestMatch.score && match.strongMatches > bestMatch.strongMatches)) {
            best = candidate;
            bestMatch = match;
            ambiguous = false;
        } else if (best && match.score == bestMatch.score && match.strongMatches == bestMatch.strongMatches
                   && candidate->storageId() != best->storageId()) {
            ambiguous = true;
        }
    }

    return ambiguous ? KService::Ptr{} : best;
}

QByteArray xProperty(Display *display, Window window, Atom property, Atom requestedType, Atom *actualType = nullptr, int *format = nullptr)
{
    Atom type = None;
    int actualFormat = 0;
    unsigned long itemCount = 0;
    unsigned long bytesAfter = 0;
    unsigned char *data = nullptr;
    const int status = XGetWindowProperty(display, window, property, 0, 16 * 1024 * 1024, False, requestedType,
                                          &type, &actualFormat, &itemCount, &bytesAfter, &data);
    if (status != Success || !data) {
        return {};
    }
    const qsizetype bytesPerItem = actualFormat == 32 ? static_cast<qsizetype>(sizeof(unsigned long)) : actualFormat / 8;
    QByteArray output(reinterpret_cast<const char *>(data), static_cast<qsizetype>(itemCount) * bytesPerItem);
    XFree(data);
    if (actualType) {
        *actualType = type;
    }
    if (format) {
        *format = actualFormat;
    }
    return output;
}

QByteArray xTextBytes(Display *display, Window window, const char *propertyName)
{
    const Atom property = XInternAtom(display, propertyName, False);
    Atom type = None;
    int format = 0;
    const QByteArray bytes = xProperty(display, window, property, AnyPropertyType, &type, &format);
    return format == 8 ? bytes : QByteArray{};
}

QString xTextProperty(Display *display, Window window, const char *propertyName)
{
    const QByteArray bytes = xTextBytes(display, window, propertyName);
    const qsizetype end = bytes.indexOf('\0');
    return QString::fromUtf8(bytes.constData(), end >= 0 ? end : bytes.size()).trimmed();
}

QJsonObject activeX11WindowResult(qint64 requestedPid, const QString &resourceClass, const QString &resourceName)
{
    Display *display = XOpenDisplay(nullptr);
    if (!display) {
        return {{QStringLiteral("found"), false}};
    }

    const Window root = DefaultRootWindow(display);
    const Atom activeProperty = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
    Atom activeType = None;
    int activeFormat = 0;
    const QByteArray activeData = xProperty(display, root, activeProperty, XA_WINDOW, &activeType, &activeFormat);
    if (activeFormat != 32 || activeData.size() < static_cast<int>(sizeof(unsigned long))) {
        XCloseDisplay(display);
        return {{QStringLiteral("found"), false}};
    }
    const Window window = *reinterpret_cast<const unsigned long *>(activeData.constData());

    const Atom pidProperty = XInternAtom(display, "_NET_WM_PID", False);
    int pidFormat = 0;
    const QByteArray pidData = xProperty(display, window, pidProperty, XA_CARDINAL, nullptr, &pidFormat);
    qint64 windowPid = 0;
    if (pidFormat == 32 && pidData.size() >= static_cast<int>(sizeof(unsigned long))) {
        windowPid = static_cast<qint64>(*reinterpret_cast<const unsigned long *>(pidData.constData()));
    }

    const QByteArray classBytes = xTextBytes(display, window, "WM_CLASS");
    const QList<QByteArray> classParts = classBytes.split('\0');
    const QString windowClass = QString::fromUtf8(classParts.size() > 1 ? classParts.at(1) : classParts.value(0)).trimmed();
    const QString title = xTextProperty(display, window, "_NET_WM_NAME");
    const bool identityMatches = (requestedPid > 1 && windowPid == requestedPid)
        || sameIdentity(windowClass, resourceClass) || sameIdentity(windowClass, resourceName);
    if (!identityMatches) {
        XCloseDisplay(display);
        return {{QStringLiteral("found"), false}};
    }

    const Atom iconProperty = XInternAtom(display, "_NET_WM_ICON", False);
    int iconFormat = 0;
    const QByteArray iconData = xProperty(display, window, iconProperty, XA_CARDINAL, nullptr, &iconFormat);
    if (iconFormat != 32 || iconData.isEmpty()) {
        XCloseDisplay(display);
        return {{QStringLiteral("found"), false}, {QStringLiteral("name"), title}};
    }

    const auto *values = reinterpret_cast<const unsigned long *>(iconData.constData());
    const qsizetype valueCount = iconData.size() / static_cast<qsizetype>(sizeof(unsigned long));
    qsizetype position = 0;
    qsizetype bestPosition = -1;
    unsigned long bestWidth = 0;
    unsigned long bestHeight = 0;
    quint64 bestScore = std::numeric_limits<quint64>::max();
    while (position + 2 <= valueCount) {
        const unsigned long width = values[position];
        const unsigned long height = values[position + 1];
        const quint64 pixels = static_cast<quint64>(width) * height;
        if (width == 0 || height == 0 || pixels > static_cast<quint64>(valueCount - position - 2)) {
            break;
        }
        const quint64 target = 96;
        const quint64 size = qMax(width, height);
        const quint64 score = size >= target ? size - target : (target - size) + target;
        if (score < bestScore) {
            bestScore = score;
            bestPosition = position + 2;
            bestWidth = width;
            bestHeight = height;
        }
        position += static_cast<qsizetype>(pixels + 2);
    }
    if (bestPosition < 0) {
        XCloseDisplay(display);
        return {{QStringLiteral("found"), false}, {QStringLiteral("name"), title}};
    }

    QImage image(static_cast<int>(bestWidth), static_cast<int>(bestHeight), QImage::Format_ARGB32);
    for (unsigned long y = 0; y < bestHeight; ++y) {
        auto *scanLine = reinterpret_cast<QRgb *>(image.scanLine(static_cast<int>(y)));
        for (unsigned long x = 0; x < bestWidth; ++x) {
            scanLine[x] = static_cast<QRgb>(values[bestPosition + y * bestWidth + x] & 0xffffffffUL);
        }
    }

    QString runtimeRoot = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtimeRoot.isEmpty()) {
        runtimeRoot = QDir::tempPath();
    }
    QDir cacheDir(runtimeRoot + QStringLiteral("/focused-volume/icons"));
    QDir().mkpath(cacheDir.path());
    QFile::setPermissions(cacheDir.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QByteArray hash = QCryptographicHash::hash(iconData, QCryptographicHash::Sha256).toHex();
    const QString iconPath = cacheDir.filePath(QStringLiteral("x11-%1-%2.png").arg(window).arg(QString::fromLatin1(hash.left(16))));
    if (!QFileInfo::exists(iconPath)) {
        QSaveFile file(iconPath);
        file.setDirectWriteFallback(true);
        if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
            XCloseDisplay(display);
            return {{QStringLiteral("found"), false}, {QStringLiteral("name"), title}};
        }
        QFile::setPermissions(iconPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    XCloseDisplay(display);
    return {
        {QStringLiteral("found"), true},
        {QStringLiteral("source"), QStringLiteral("x11-window")},
        {QStringLiteral("name"), title},
        {QStringLiteral("icon"), iconPath},
        {QStringLiteral("pid"), windowPid},
        {QStringLiteral("windowClass"), windowClass},
        {QStringLiteral("windowId"), QStringLiteral("0x%1").arg(window, 0, 16)},
    };
}

void printJson(const QJsonObject &object)
{
    QTextStream output(stdout);
    output << QJsonDocument(object).toJson(QJsonDocument::Compact) << '\n';
    output.flush();
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("focused-volume-app-resolver"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Resolve KWin application metadata through KDE KService"));
    parser.addHelpOption();

    const QCommandLineOption activeX11Option(QStringLiteral("active-x11"), QStringLiteral("Resolve the active X11/XWayland window name and icon"));
    const QCommandLineOption desktopFileOption(QStringLiteral("desktop-file"), QStringLiteral("KWin desktopFile value"), QStringLiteral("value"));
    const QCommandLineOption pidOption(QStringLiteral("pid"), QStringLiteral("Focused process ID"), QStringLiteral("pid"), QStringLiteral("0"));
    const QCommandLineOption resourceClassOption(QStringLiteral("resource-class"), QStringLiteral("KWin resourceClass value"), QStringLiteral("value"));
    const QCommandLineOption resourceNameOption(QStringLiteral("resource-name"), QStringLiteral("KWin resourceName value"), QStringLiteral("value"));

    parser.addOptions({activeX11Option, desktopFileOption, pidOption, resourceClassOption, resourceNameOption});
    parser.process(app);

    bool pidOk = false;
    const qint64 pid = parser.value(pidOption).toLongLong(&pidOk);
    if (!pidOk || pid < 0) {
        printJson({{QStringLiteral("found"), false}, {QStringLiteral("error"), QStringLiteral("invalid pid")}});
        return 2;
    }

    if (parser.isSet(activeX11Option)) {
        const QJsonObject result = activeX11WindowResult(pid, parser.value(resourceClassOption), parser.value(resourceNameOption));
        printJson(result);
        return result.value(QStringLiteral("found")).toBool() ? 0 : 3;
    }

    const Identity identity = collectIdentity(parser.value(desktopFileOption), pid,
                                              parser.value(resourceClassOption), parser.value(resourceNameOption));

    KService::Ptr service = directService(identity.desktopFile);
    if (!service) {
        service = tradedService(identity);
    }
    service = followAliases(service);

    if (!service) {
        printJson({{QStringLiteral("found"), false}});
        return 3;
    }

    const QString executable = executableFromCommand(service->exec());
    const QJsonObject result{
        {QStringLiteral("found"), true},
        {QStringLiteral("desktopId"), service->desktopEntryName()},
        {QStringLiteral("storageId"), service->storageId()},
        {QStringLiteral("desktopPath"), service->entryPath()},
        {QStringLiteral("name"), service->name()},
        {QStringLiteral("icon"), service->icon()},
        {QStringLiteral("exec"), service->exec()},
        {QStringLiteral("executable"), executable},
        {QStringLiteral("startupWMClass"), service->property<QString>(QStringLiteral("StartupWMClass"))},
    };
    printJson(result);
    return 0;
}
