// SPDX-License-Identifier: GPL-3.0-or-later

function reportActiveWindow(window) {
    if (!window || !window.active) {
        callDBus("net.local.MediaVol.FocusedVolume", "/FocusedVolume",
            "net.local.MediaVol.FocusedVolume", "UpdateFocus",
            "", "0", "", "", "", "", false);
        return;
    }

    callDBus("net.local.MediaVol.FocusedVolume", "/FocusedVolume",
        "net.local.MediaVol.FocusedVolume", "UpdateFocus",
        String(window.internalId), String(window.pid || 0),
        window.desktopFileName || "", window.resourceClass || "",
        window.resourceName || "", window.caption || "",
        Boolean(window.fullScreen));
}

workspace.windowActivated.connect(reportActiveWindow);
reportActiveWindow(workspace.activeWindow);
