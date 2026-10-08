#pragma once

#include <QString>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <fstream>
#include <string>

namespace gnumon::gui {

struct AppConfig {
    // 1. Metrics Selection
    bool showPresentFps = true;
    bool showDisplayedFps = true;
    bool showFps1PercentLow = true;
    bool showFrameTime = true;
    bool showLatency = true;
    bool showAnimationError = true;

    bool showGpuPower = true;
    bool showGpuTemp = true;
    bool showGpuFreq = true;
    bool showGpuUtil = true;
    bool showGpuVram = true;

    bool showCpuPower = true;
    bool showCpuTemp = true;
    bool showCpuFreq = true;
    bool showCpuUtil = true;

    bool showGraph = true;
    int averagingWindowMs = 1000;

    // 2. Desktop Overlay Appearance
    int overlayCorner = 0; // 0: Top-Left, 1: Top-Right, 2: Bottom-Left, 3: Bottom-Right
    int overlayScale = 100; // 75, 100, 125, 150
    int overlayOpacity = 85; // 20 - 100
    bool overlayShowGraph = true;

    // 3. In-Game Vulkan Swapchain HUD
    bool inGameHudEnabled = false;
    int inGameHudCorner = 0; // 0: Top-Left, 1: Top-Right, 2: Bottom-Left, 3: Bottom-Right

    // 4. Hotkeys
    QString hotkeyOverlay = "F11";
    QString hotkeyRecording = "F10";
    QString hotkeyInGameHud = "F9";
    QString hotkeyMiniHud = "F12";

    // 5. Captures (CSV)
    QString captureDirectory = "";
    bool autoOpenCaptures = false;

    static QString GetConfigFilePath() {
        QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
        if (configDir.isEmpty()) {
            configDir = QDir::homePath() + "/.config";
        }
        configDir += "/gnumon";
        QDir().mkpath(configDir);
        return configDir + "/config.ini";
    }

    void Load() {
        QString path = GetConfigFilePath();
        QSettings s(path, QSettings::IniFormat);

        s.beginGroup("Metrics");
        showPresentFps = s.value("showPresentFps", showPresentFps).toBool();
        showDisplayedFps = s.value("showDisplayedFps", showDisplayedFps).toBool();
        showFps1PercentLow = s.value("showFps1PercentLow", showFps1PercentLow).toBool();
        showFrameTime = s.value("showFrameTime", showFrameTime).toBool();
        showLatency = s.value("showLatency", showLatency).toBool();
        showAnimationError = s.value("showAnimationError", showAnimationError).toBool();

        showGpuPower = s.value("showGpuPower", showGpuPower).toBool();
        showGpuTemp = s.value("showGpuTemp", showGpuTemp).toBool();
        showGpuFreq = s.value("showGpuFreq", showGpuFreq).toBool();
        showGpuUtil = s.value("showGpuUtil", showGpuUtil).toBool();
        showGpuVram = s.value("showGpuVram", showGpuVram).toBool();

        showCpuPower = s.value("showCpuPower", showCpuPower).toBool();
        showCpuTemp = s.value("showCpuTemp", showCpuTemp).toBool();
        showCpuFreq = s.value("showCpuFreq", showCpuFreq).toBool();
        showCpuUtil = s.value("showCpuUtil", showCpuUtil).toBool();

        showGraph = s.value("showGraph", showGraph).toBool();
        averagingWindowMs = s.value("averagingWindowMs", averagingWindowMs).toInt();
        s.endGroup();

        s.beginGroup("Overlay");
        overlayCorner = s.value("corner", overlayCorner).toInt();
        overlayScale = s.value("scale", overlayScale).toInt();
        overlayOpacity = s.value("opacity", overlayOpacity).toInt();
        overlayShowGraph = s.value("showGraph", overlayShowGraph).toBool();
        inGameHudEnabled = s.value("inGameHudEnabled", inGameHudEnabled).toBool();
        inGameHudCorner = s.value("inGameHudCorner", inGameHudCorner).toInt();
        s.endGroup();

        s.beginGroup("Hotkeys");
        hotkeyOverlay = s.value("toggleOverlay", hotkeyOverlay).toString();
        hotkeyRecording = s.value("toggleRecording", hotkeyRecording).toString();
        hotkeyInGameHud = s.value("toggleInGameHud", hotkeyInGameHud).toString();
        hotkeyMiniHud = s.value("toggleMiniHud", hotkeyMiniHud).toString();
        s.endGroup();

        s.beginGroup("Capture");
        QString defaultDocs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (defaultDocs.isEmpty()) defaultDocs = QDir::homePath() + "/Documents";
        captureDirectory = s.value("directory", defaultDocs + "/gnumon/captures").toString();
        autoOpenCaptures = s.value("autoOpen", autoOpenCaptures).toBool();
        s.endGroup();
    }

    void Save() const {
        QString path = GetConfigFilePath();
        QSettings s(path, QSettings::IniFormat);

        s.beginGroup("Metrics");
        s.setValue("showPresentFps", showPresentFps);
        s.setValue("showDisplayedFps", showDisplayedFps);
        s.setValue("showFps1PercentLow", showFps1PercentLow);
        s.setValue("showFrameTime", showFrameTime);
        s.setValue("showLatency", showLatency);
        s.setValue("showAnimationError", showAnimationError);

        s.setValue("showGpuPower", showGpuPower);
        s.setValue("showGpuTemp", showGpuTemp);
        s.setValue("showGpuFreq", showGpuFreq);
        s.setValue("showGpuUtil", showGpuUtil);
        s.setValue("showGpuVram", showGpuVram);

        s.setValue("showCpuPower", showCpuPower);
        s.setValue("showCpuTemp", showCpuTemp);
        s.setValue("showCpuFreq", showCpuFreq);
        s.setValue("showCpuUtil", showCpuUtil);

        s.setValue("showGraph", showGraph);
        s.setValue("averagingWindowMs", averagingWindowMs);
        s.endGroup();

        s.beginGroup("Overlay");
        s.setValue("corner", overlayCorner);
        s.setValue("scale", overlayScale);
        s.setValue("opacity", overlayOpacity);
        s.setValue("showGraph", overlayShowGraph);
        s.setValue("inGameHudEnabled", inGameHudEnabled);
        s.setValue("inGameHudCorner", inGameHudCorner);
        s.endGroup();

        s.beginGroup("Hotkeys");
        s.setValue("toggleOverlay", hotkeyOverlay);
        s.setValue("toggleRecording", hotkeyRecording);
        s.setValue("toggleInGameHud", hotkeyInGameHud);
        s.setValue("toggleMiniHud", hotkeyMiniHud);
        s.endGroup();

        s.beginGroup("Capture");
        s.setValue("directory", captureDirectory);
        s.setValue("autoOpen", autoOpenCaptures);
        s.endGroup();

        s.sync();
    }
};

} // namespace gnumon::gui
