#include <KService>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QSet>
#include <QTimer>

#include <pulse/pulseaudio.h>

#include <algorithm>
#include <optional>
#include <unistd.h>

namespace
{
struct Client {
    uint32_t index = PA_INVALID_INDEX;
    qint64 pid = 0;
};

struct Stream {
    uint32_t index = PA_INVALID_INDEX;
    uint32_t client = PA_INVALID_INDEX;
    qint64 pid = 0;
    QString name;
    QString binary;
    QString appId;
    QString desktopId;
    QString icon;
    pa_cvolume volume{};
};

struct Focus {
    QString uuid;
    qint64 pid = 0;
    QString desktopId;
    QString resourceClass;
    QString resourceName;
    QString caption;
    bool fullscreen = false;
    quint64 generation = 0;
    QString name;
    QString icon;
};

qint64 propertyPid(pa_proplist *properties, const char *key)
{
    const char *value = pa_proplist_gets(properties, key);
    if (!value) {
        return 0;
    }
    bool ok = false;
    const qint64 pid = QString::fromUtf8(value).toLongLong(&ok);
    if (!ok || pid <= 1 || !QFileInfo::exists(QStringLiteral("/proc/%1/status").arg(pid))) {
        return 0;
    }
    QFile status(QStringLiteral("/proc/%1/status").arg(pid));
    if (!status.open(QIODevice::ReadOnly)) {
        return 0;
    }
    const QList<QByteArray> lines = status.readAll().split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith("Uid:")) {
            const QList<QByteArray> fields = line.simplified().split(' ');
            return fields.size() > 1 && fields.at(1).toUInt() == getuid() ? pid : 0;
        }
    }
    return 0;
}

QString propertyString(pa_proplist *properties, const char *key)
{
    const char *value = pa_proplist_gets(properties, key);
    return value ? QString::fromUtf8(value) : QString();
}

QString parentCgroup(qint64 pid)
{
    QFile file(QStringLiteral("/proc/%1/cgroup").arg(pid));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QList<QByteArray> lines = file.readAll().split('\n');
    for (const QByteArray &line : lines) {
        if (!line.startsWith("0::")) {
            continue;
        }
        QString result;
        for (const QByteArray &part : line.mid(3).split('/')) {
            const QString component = QString::fromUtf8(part);
            if ((component.startsWith(QStringLiteral("app-")) && component.endsWith(QStringLiteral(".scope")))
                || (component.startsWith(QStringLiteral("app-")) && component.endsWith(QStringLiteral(".service")))) {
                result = component;
            }
        }
        return result;
    }
    return {};
}

qint64 parentPid(qint64 pid)
{
    QFile file(QStringLiteral("/proc/%1/status").arg(pid));
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }
    const QList<QByteArray> lines = file.readAll().split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith("PPid:")) {
            return line.mid(5).trimmed().toLongLong();
        }
    }
    return 0;
}

bool isAncestor(qint64 ancestor, qint64 pid)
{
    for (int depth = 0; pid > 1 && depth < 64; ++depth) {
        if (pid == ancestor) {
            return true;
        }
        const qint64 parent = parentPid(pid);
        if (parent <= 1 || parent == pid) {
            break;
        }
        pid = parent;
    }
    return false;
}

QString normalized(QString value)
{
    value = value.trimmed().toLower();
    if (value.endsWith(QStringLiteral(".desktop"))) {
        value.chop(8);
    }
    return value;
}
}

class FocusedVolume : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.local.MediaVol.FocusedVolume")

public:
    explicit FocusedVolume(QObject *parent = nullptr)
        : QObject(parent)
    {
        m_mainloop = pa_threaded_mainloop_new();
        m_context = pa_context_new(pa_threaded_mainloop_get_api(m_mainloop), "MediaVol Focused Volume");
        pa_context_set_state_callback(m_context, &FocusedVolume::contextState, this);
        pa_context_connect(m_context, nullptr, PA_CONTEXT_NOFLAGS, nullptr);
        pa_threaded_mainloop_start(m_mainloop);
    }

    ~FocusedVolume() override
    {
        if (m_context) {
            pa_context_disconnect(m_context);
            pa_context_unref(m_context);
        }
        if (m_mainloop) {
            pa_threaded_mainloop_stop(m_mainloop);
            pa_threaded_mainloop_free(m_mainloop);
        }
    }

public Q_SLOTS:
    void UpdateFocus(const QString &uuid, const QString &pidValue, const QString &desktopId,
                     const QString &resourceClass, const QString &resourceName,
                     const QString &caption, bool fullscreen)
    {
        const qlonglong pid = pidValue.toLongLong();
        QMutexLocker locker(&m_mutex);
        if (m_focus.uuid == uuid && m_focus.pid == pid && m_focus.desktopId == desktopId
            && m_focus.resourceClass == resourceClass && m_focus.resourceName == resourceName) {
            m_focus.caption = caption;
            m_focus.fullscreen = fullscreen;
            return;
        }
        m_focus = {uuid, pid, desktopId, resourceClass, resourceName, caption, fullscreen,
                   m_focus.generation + 1, {}, {}};
        resolvePresentationLocked();
        rematchLocked();
    }

    bool Adjust(int direction)
    {
        if (direction != -1 && direction != 1) {
            return false;
        }

        QMutexLocker locker(&m_mutex);
        if (!m_ready || m_matches.isEmpty()) {
            return false;
        }
        const quint64 focusGeneration = m_focus.generation;
        const quint64 graphGeneration = m_graphGeneration;
        const QList<uint32_t> matches = m_matches;
        int maximumPercent = 0;

        pa_threaded_mainloop_lock(m_mainloop);
        for (uint32_t index : matches) {
            auto it = m_streams.find(index);
            if (it == m_streams.end() || focusGeneration != m_focus.generation || graphGeneration != m_graphGeneration) {
                pa_threaded_mainloop_unlock(m_mainloop);
                return false;
            }
            Stream &stream = it.value();
            for (uint8_t channel = 0; channel < stream.volume.channels; ++channel) {
                const int target = std::clamp(static_cast<int>(stream.volume.values[channel]) + direction * 3277,
                                              0, static_cast<int>(PA_VOLUME_NORM));
                stream.volume.values[channel] = static_cast<pa_volume_t>(target);
            }
            pa_operation *operation = pa_context_set_sink_input_volume(m_context, index, &stream.volume, nullptr, nullptr);
            if (operation) {
                pa_operation_unref(operation);
            }
            maximumPercent = std::max(maximumPercent,
                                      static_cast<int>(qRound(pa_cvolume_avg(&stream.volume) * 100.0 / PA_VOLUME_NORM)));
        }
        pa_threaded_mainloop_unlock(m_mainloop);

        const QString name = m_focus.name.isEmpty() ? QStringLiteral("Focused application") : m_focus.name;
        const QString icon = m_focus.icon.isEmpty() ? QStringLiteral("audio-volume-high") : m_focus.icon;
        locker.unlock();
        QDBusMessage osd = QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"),
                                                         QStringLiteral("/org/kde/osdService"),
                                                         QStringLiteral("org.kde.osdService"),
                                                         QStringLiteral("mediaPlayerVolumeChanged"));
        osd << maximumPercent << name << icon;
        QDBusConnection::sessionBus().asyncCall(osd);
        return true;
    }

    QString Diagnose()
    {
        QMutexLocker locker(&m_mutex);
        QJsonArray streams;
        for (uint32_t index : m_matches) {
            const Stream stream = m_streams.value(index);
            streams.append(QJsonObject{{QStringLiteral("index"), static_cast<int>(index)},
                                       {QStringLiteral("client"), static_cast<int>(stream.client)},
                                       {QStringLiteral("pid"), stream.pid},
                                       {QStringLiteral("name"), stream.name},
                                       {QStringLiteral("percent"), qRound(pa_cvolume_avg(&stream.volume) * 100.0 / PA_VOLUME_NORM)}});
        }
        return QString::fromUtf8(QJsonDocument(QJsonObject{
            {QStringLiteral("ready"), m_ready},
            {QStringLiteral("focusGeneration"), static_cast<qint64>(m_focus.generation)},
            {QStringLiteral("graphGeneration"), static_cast<qint64>(m_graphGeneration)},
            {QStringLiteral("focus"), QJsonObject{{QStringLiteral("uuid"), m_focus.uuid},
                                                  {QStringLiteral("pid"), m_focus.pid},
                                                  {QStringLiteral("name"), m_focus.name},
                                                  {QStringLiteral("icon"), m_focus.icon},
                                                  {QStringLiteral("fullscreen"), m_focus.fullscreen}}},
            {QStringLiteral("streams"), streams},
        }).toJson(QJsonDocument::Compact));
    }

private:
    static void contextState(pa_context *context, void *userdata)
    {
        auto *self = static_cast<FocusedVolume *>(userdata);
        if (pa_context_get_state(context) != PA_CONTEXT_READY) {
            return;
        }
        pa_context_set_subscribe_callback(context, &FocusedVolume::subscription, self);
        pa_operation *operation = pa_context_subscribe(context,
            static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_CLIENT | PA_SUBSCRIPTION_MASK_SINK_INPUT), nullptr, nullptr);
        if (operation) pa_operation_unref(operation);
        self->refreshAll();
    }

    static void subscription(pa_context *, pa_subscription_event_type_t type, uint32_t index, void *userdata)
    {
        auto *self = static_cast<FocusedVolume *>(userdata);
        const auto facility = static_cast<pa_subscription_event_type_t>(type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK);
        const auto event = static_cast<pa_subscription_event_type_t>(type & PA_SUBSCRIPTION_EVENT_TYPE_MASK);
        if (facility == PA_SUBSCRIPTION_EVENT_CLIENT) {
            if (event == PA_SUBSCRIPTION_EVENT_REMOVE) {
                QMutexLocker locker(&self->m_mutex);
                self->m_clients.remove(index);
                ++self->m_graphGeneration;
                self->rematchLocked();
            } else {
                pa_operation *operation = pa_context_get_client_info(self->m_context, index, &FocusedVolume::clientInfo, self);
                if (operation) pa_operation_unref(operation);
            }
        } else if (facility == PA_SUBSCRIPTION_EVENT_SINK_INPUT) {
            if (event == PA_SUBSCRIPTION_EVENT_REMOVE) {
                QMutexLocker locker(&self->m_mutex);
                self->m_streams.remove(index);
                ++self->m_graphGeneration;
                self->rematchLocked();
            } else {
                pa_operation *operation = pa_context_get_sink_input_info(self->m_context, index, &FocusedVolume::sinkInfo, self);
                if (operation) pa_operation_unref(operation);
            }
        }
    }

    static void clientInfo(pa_context *, const pa_client_info *info, int eol, void *userdata)
    {
        if (eol || !info) return;
        auto *self = static_cast<FocusedVolume *>(userdata);
        Client client;
        client.index = info->index;
        client.pid = propertyPid(info->proplist, PA_PROP_APPLICATION_PROCESS_ID);
        if (!client.pid) client.pid = propertyPid(info->proplist, "pipewire.sec.pid");
        QMutexLocker locker(&self->m_mutex);
        self->m_clients[client.index] = client;
        ++self->m_graphGeneration;
        self->rematchLocked();
    }

    static void sinkInfo(pa_context *, const pa_sink_input_info *info, int eol, void *userdata)
    {
        if (eol || !info) return;
        auto *self = static_cast<FocusedVolume *>(userdata);
        Stream stream;
        stream.index = info->index;
        stream.client = info->client;
        stream.pid = propertyPid(info->proplist, PA_PROP_APPLICATION_PROCESS_ID);
        stream.name = propertyString(info->proplist, PA_PROP_APPLICATION_NAME);
        stream.binary = propertyString(info->proplist, PA_PROP_APPLICATION_PROCESS_BINARY);
        stream.appId = propertyString(info->proplist, "application.id");
        stream.desktopId = propertyString(info->proplist, "application.desktop");
        stream.icon = propertyString(info->proplist, PA_PROP_APPLICATION_ICON_NAME);
        stream.volume = info->volume;
        QMutexLocker locker(&self->m_mutex);
        self->m_streams[stream.index] = stream;
        ++self->m_graphGeneration;
        self->rematchLocked();
    }

    void refreshAll()
    {
        pa_operation *clients = pa_context_get_client_info_list(m_context, &FocusedVolume::clientInfo, this);
        if (clients) pa_operation_unref(clients);
        pa_operation *streams = pa_context_get_sink_input_info_list(m_context, &FocusedVolume::sinkInfo, this);
        if (streams) pa_operation_unref(streams);
        QMutexLocker locker(&m_mutex);
        m_ready = true;
    }

    void resolvePresentationLocked()
    {
        m_focus.name.clear();
        m_focus.icon.clear();
        const QString resolverPath = QDir::homePath() + QStringLiteral("/.local/bin/focused-volume-app-resolver");
        QProcess serviceResolver;
        serviceResolver.start(resolverPath,
                              {QStringLiteral("--desktop-file"), m_focus.desktopId,
                               QStringLiteral("--pid"), QString::number(m_focus.pid),
                               QStringLiteral("--resource-class"), m_focus.resourceClass,
                               QStringLiteral("--resource-name"), m_focus.resourceName});
        if (serviceResolver.waitForFinished(500)) {
            const QJsonObject result = QJsonDocument::fromJson(serviceResolver.readAllStandardOutput()).object();
            if (result.value(QStringLiteral("found")).toBool()) {
                m_focus.name = result.value(QStringLiteral("name")).toString();
                m_focus.icon = result.value(QStringLiteral("icon")).toString();
            }
        }
        if (m_focus.name.isEmpty() || m_focus.icon.isEmpty()) {
            QProcess x11Resolver;
            x11Resolver.start(resolverPath,
                              {QStringLiteral("--active-x11"), QStringLiteral("--pid"), QString::number(m_focus.pid),
                               QStringLiteral("--resource-class"), m_focus.resourceClass,
                               QStringLiteral("--resource-name"), m_focus.resourceName});
            if (x11Resolver.waitForFinished(500)) {
                const QJsonObject result = QJsonDocument::fromJson(x11Resolver.readAllStandardOutput()).object();
                if (result.value(QStringLiteral("found")).toBool()) {
                    if (m_focus.name.isEmpty()) {
                        m_focus.name = result.value(QStringLiteral("name")).toString();
                    }
                    if (m_focus.icon.isEmpty()) {
                        m_focus.icon = result.value(QStringLiteral("icon")).toString();
                    }
                }
            }
        }
        if (m_focus.name.isEmpty()) {
            m_focus.name = !m_focus.caption.isEmpty() ? m_focus.caption
                : (!m_focus.resourceClass.isEmpty() ? m_focus.resourceClass : QStringLiteral("Focused application"));
        }
    }

    void rematchLocked()
    {
        m_matches.clear();
        if (m_focus.pid <= 1) return;

        for (auto it = m_streams.begin(); it != m_streams.end(); ++it) {
            Stream &stream = it.value();
            if (!stream.pid && m_clients.contains(stream.client)) stream.pid = m_clients.value(stream.client).pid;
            if (stream.pid > 1 && (isAncestor(m_focus.pid, stream.pid) || isAncestor(stream.pid, m_focus.pid))) {
                m_matches.append(stream.index);
            }
        }
        if (!m_matches.isEmpty()) return;

        const QString focusCgroup = parentCgroup(m_focus.pid);
        if (!focusCgroup.isEmpty()) {
            for (const Stream &stream : std::as_const(m_streams)) {
                if (stream.pid > 1 && parentCgroup(stream.pid) == focusCgroup) m_matches.append(stream.index);
            }
        }
        if (!m_matches.isEmpty()) return;

        const QString focusDesktop = normalized(m_focus.desktopId);
        const QString focusClass = normalized(m_focus.resourceClass);
        for (const Stream &stream : std::as_const(m_streams)) {
            const bool strong = (!focusDesktop.isEmpty()
                && (normalized(stream.appId) == focusDesktop || normalized(stream.desktopId) == focusDesktop));
            int score = 0;
            if (!stream.binary.isEmpty() && normalized(stream.binary) == focusClass) ++score;
            if (!stream.name.isEmpty() && normalized(stream.name) == focusClass) ++score;
            if (strong || score >= 2) m_matches.append(stream.index);
        }
    }

    pa_threaded_mainloop *m_mainloop = nullptr;
    pa_context *m_context = nullptr;
    QMutex m_mutex;
    QHash<uint32_t, Client> m_clients;
    QHash<uint32_t, Stream> m_streams;
    Focus m_focus;
    QList<uint32_t> m_matches;
    quint64 m_graphGeneration = 0;
    bool m_ready = false;
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("focused-volume-daemon"));
    const QStringList arguments = app.arguments();

    if (arguments.contains(QStringLiteral("--daemon"))) {
        FocusedVolume daemon;
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.registerService(QStringLiteral("net.local.MediaVol.FocusedVolume"))
            || !bus.registerObject(QStringLiteral("/FocusedVolume"), &daemon, QDBusConnection::ExportAllSlots)) {
            return 1;
        }
        return app.exec();
    }

    QDBusInterface daemon(QStringLiteral("net.local.MediaVol.FocusedVolume"), QStringLiteral("/FocusedVolume"),
                          QStringLiteral("net.local.MediaVol.FocusedVolume"), QDBusConnection::sessionBus());
    if (!daemon.isValid()) return 1;
    if (arguments.contains(QStringLiteral("diagnose"))) {
        const QDBusMessage reply = daemon.call(QStringLiteral("Diagnose"));
        if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) return 1;
        QTextStream(stdout) << reply.arguments().constFirst().toString() << '\n';
        return 0;
    }
    const int direction = arguments.contains(QStringLiteral("down")) ? -1 : arguments.contains(QStringLiteral("up")) ? 1 : 0;
    if (!direction) return 2;
    const QDBusMessage reply = daemon.call(QStringLiteral("Adjust"), direction);
    return reply.type() != QDBusMessage::ErrorMessage && !reply.arguments().isEmpty() && reply.arguments().constFirst().toBool() ? 0 : 1;
}

#include "main.moc"
