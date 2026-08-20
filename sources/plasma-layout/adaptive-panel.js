const markerGroup = ["MediaVol"];
const markerKey = "managedPanel";
const markerValue = "adaptive-bottom-panel";
const revisionKey = "revision";
const revision = "2";
const applyKey = "applyId";
const applyId = "__MEDIAVOL_APPLY_ID__";
const wallpaper = "__MEDIAVOL_WALLPAPER__";
const createdPanelIds = [];

for (const desktop of desktops()) {
    desktop.wallpaperPlugin = "org.kde.image";
    desktop.currentConfigGroup = ["Wallpaper", "org.kde.image", "General"];
    desktop.writeConfig("Image", "file://" + wallpaper);
}

for (const panel of panels()) {
    panel.currentConfigGroup = markerGroup;
    if (panel.readConfig(markerKey) === markerValue) {
        panel.remove();
    }
}

for (let screen = 0; screen < screenCount; ++screen) {
    const panel = new Panel;
    panel.location = "bottom";
    panel.height = 40;
    panel.currentConfigGroup = markerGroup;
    panel.writeConfig(markerKey, markerValue);
    panel.writeConfig(revisionKey, revision);
    panel.writeConfig(applyKey, applyId);

    panel.addWidget("org.kde.plasma.kickoff");
    panel.addWidget("org.kde.plasma.panelspacer");
    panel.addWidget("org.kde.plasma.icontasks");
    panel.addWidget("org.kde.plasma.panelspacer");
    panel.addWidget("plasmusic-toolbar");
    panel.addWidget("org.kde.plasma.systemtray");
    const clock = panel.addWidget("org.kde.plasma.digitalclock");
    clock.currentConfigGroup = ["Appearance"];
    clock.writeConfig("dateDisplayFormat", "BelowTime");
    clock.writeConfig("dateFormat", "custom");
    clock.writeConfig("customDateFormat", "dd/MM/yy");
    clock.writeConfig("use24hFormat", "0");
    createdPanelIds.push(panel.id);
}

JSON.stringify(createdPanelIds);
