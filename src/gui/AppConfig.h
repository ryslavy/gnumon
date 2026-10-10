#pragma once

#include <QString>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QColor>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <fstream>
#include <string>

namespace gnumon::gui {

enum class WidgetType {
    Graph = 0,
    Readout = 1
};

enum class GraphType {
    Line = 0,
    Histogram = 1
};

struct LoadoutMetricItem {
    int metricId = 12; // PM_METRIC id
    int statId = 1;   // 0: None/Raw, 1: Avg, 5: 1% Low, 6: 99% Low, 2: Min, 3: Max
    int deviceId = 0;
    QColor lineColor = QColor(100, 255, 255, 220);
    QColor fillColor = QColor(57, 210, 250, 25);
    int axisAffinity = 0; // 0: Left, 1: Right
};

struct LoadoutWidget {
    int key = 0;
    WidgetType widgetType = WidgetType::Readout;
    GraphType graphType = GraphType::Line;
    float rangeMin = 0.0f;
    float rangeMax = 50.0f;
    bool autoScale = true;
    QVector<LoadoutMetricItem> metrics;
};

struct LoadoutConfig {
    QVector<LoadoutWidget> widgets;

    static LoadoutConfig MakeDefaultBasic() {
        LoadoutConfig c;
        // Row 1: GPU Name (Readout)
        {
            LoadoutWidget w;
            w.key = 0;
            w.widgetType = WidgetType::Readout;
            LoadoutMetricItem m;
            m.metricId = 3; // GPU Name
            m.statId = 0;
            w.metrics.append(m);
            c.widgets.append(w);
        }
        // Row 2: FPS-Presents avg (Readout)
        {
            LoadoutWidget w;
            w.key = 1;
            w.widgetType = WidgetType::Readout;
            LoadoutMetricItem m;
            m.metricId = 12; // FPS-Presents
            m.statId = 1;   // avg
            w.metrics.append(m);
            c.widgets.append(w);
        }
        // Row 3: FPS-Presents 1% (Readout)
        {
            LoadoutWidget w;
            w.key = 2;
            w.widgetType = WidgetType::Readout;
            LoadoutMetricItem m;
            m.metricId = 12; // FPS-Presents
            m.statId = 5;   // 1% low
            w.metrics.append(m);
            c.widgets.append(w);
        }
        // Row 4: FrameTime-Presents avg (Graph Line)
        {
            LoadoutWidget w;
            w.key = 3;
            w.widgetType = WidgetType::Graph;
            w.graphType = GraphType::Line;
            w.rangeMin = 0.0f;
            w.rangeMax = 50.0f;
            w.autoScale = true;
            LoadoutMetricItem m;
            m.metricId = 87; // FrameTime-Presents
            m.statId = 1;
            m.lineColor = QColor(0, 229, 255);
            m.fillColor = QColor(0, 180, 216, 40);
            w.metrics.append(m);
            c.widgets.append(w);
        }
        return c;
    }

    static LoadoutConfig MakeGameExperience() {
        LoadoutConfig c;
        // Widget 0: Until Displayed (Graph, avg + raw)
        {
            LoadoutWidget w;
            w.key = 0;
            w.widgetType = WidgetType::Graph;
            w.graphType = GraphType::Line;
            w.rangeMin = 0.0f;
            w.rangeMax = 1.0f;
            w.autoScale = true;

            LoadoutMetricItem mAvg;
            mAvg.metricId = 139; // Until Displayed
            mAvg.statId = 1;     // avg
            mAvg.lineColor = QColor(0, 229, 255);
            mAvg.fillColor = QColor(0, 180, 216, 50);
            w.metrics.append(mAvg);

            LoadoutMetricItem mRaw;
            mRaw.metricId = 139;
            mRaw.statId = 4;     // raw
            mRaw.lineColor = QColor(105, 240, 174);
            w.metrics.append(mRaw);

            c.widgets.append(w);
        }
        // Widget 1: Between Display Change (Graph, avg + 99% + raw)
        {
            LoadoutWidget w;
            w.key = 1;
            w.widgetType = WidgetType::Graph;
            w.graphType = GraphType::Line;
            w.rangeMin = 9.0f;
            w.rangeMax = 11.0f;
            w.autoScale = true;

            LoadoutMetricItem mAvg;
            mAvg.metricId = 138; // Between Display Change
            mAvg.statId = 1;     // avg
            mAvg.lineColor = QColor(0, 229, 255);
            mAvg.fillColor = QColor(0, 180, 216, 60);
            w.metrics.append(mAvg);

            LoadoutMetricItem m99;
            m99.metricId = 138;
            m99.statId = 6;     // 99%
            m99.lineColor = QColor(255, 82, 82);
            w.metrics.append(m99);

            LoadoutMetricItem mRaw;
            mRaw.metricId = 138;
            mRaw.statId = 4;     // raw
            mRaw.lineColor = QColor(105, 240, 174);
            w.metrics.append(mRaw);

            c.widgets.append(w);
        }
        // Widget 2: Dropped Frames (Graph, avg)
        {
            LoadoutWidget w;
            w.key = 2;
            w.widgetType = WidgetType::Graph;
            w.graphType = GraphType::Line;
            w.rangeMin = 0.0f;
            w.rangeMax = 1.0f;
            w.autoScale = false;

            LoadoutMetricItem mAvg;
            mAvg.metricId = 144; // Dropped Frames
            mAvg.statId = 1;     // avg
            mAvg.lineColor = QColor(0, 229, 255);
            w.metrics.append(mAvg);

            c.widgets.append(w);
        }
        // Widget 3: FPS-Presents (Readout)
        {
            LoadoutWidget w;
            w.key = 3;
            w.widgetType = WidgetType::Readout;
            LoadoutMetricItem m;
            m.metricId = 12; // FPS
            m.statId = 1;    // avg
            w.metrics.append(m);
            c.widgets.append(w);
        }
        // Widget 4: GPU Utilization (Readout)
        {
            LoadoutWidget w;
            w.key = 4;
            w.widgetType = WidgetType::Readout;
            LoadoutMetricItem m;
            m.metricId = 33; // GPU Util
            m.statId = 1;
            w.metrics.append(m);
            c.widgets.append(w);
        }
        // Widget 5: CPU Utilization (Readout)
        {
            LoadoutWidget w;
            w.key = 5;
            w.widgetType = WidgetType::Readout;
            LoadoutMetricItem m;
            m.metricId = 34; // CPU Util
            m.statId = 1;
            w.metrics.append(m);
            c.widgets.append(w);
        }
        return c;
    }

    static LoadoutConfig MakeGpuFocus() {
        LoadoutConfig c = MakeDefaultBasic();
        // Add GPU Util, Power, Temp, VRAM
        {
            LoadoutWidget w;
            w.key = 4;
            w.widgetType = WidgetType::Readout;
            LoadoutMetricItem m;
            m.metricId = 33; // GPU Util
            m.statId = 1;
            w.metrics.append(m);
            c.widgets.append(w);
        }
        {
            LoadoutWidget w;
            w.key = 5;
            w.widgetType = WidgetType::Readout;
            LoadoutMetricItem m;
            m.metricId = 31; // GPU Temp
            m.statId = 1;
            w.metrics.append(m);
            c.widgets.append(w);
        }
        {
            LoadoutWidget w;
            w.key = 6;
            w.widgetType = WidgetType::Readout;
            LoadoutMetricItem m;
            m.metricId = 28; // GPU Power
            m.statId = 1;
            w.metrics.append(m);
            c.widgets.append(w);
        }
        return c;
    }

    static LoadoutConfig MakePowerTemp() {
        LoadoutConfig c;
        {
            LoadoutWidget w;
            w.key = 0;
            w.widgetType = WidgetType::Readout;
            LoadoutMetricItem m;
            m.metricId = 3; // GPU Name
            m.statId = 0;
            w.metrics.append(m);
            c.widgets.append(w);
        }
        {
            LoadoutWidget w;
            w.key = 1;
            w.widgetType = WidgetType::Readout;
            LoadoutMetricItem m;
            m.metricId = 12; // FPS
            m.statId = 1;
            w.metrics.append(m);
            c.widgets.append(w);
        }
        {
            LoadoutWidget w;
            w.key = 2;
            w.widgetType = WidgetType::Readout;
            LoadoutMetricItem m;
            m.metricId = 28; // GPU Power
            m.statId = 1;
            w.metrics.append(m);
            c.widgets.append(w);
        }
        {
            LoadoutWidget w;
            w.key = 3;
            w.widgetType = WidgetType::Readout;
            LoadoutMetricItem m;
            m.metricId = 31; // GPU Temp
            m.statId = 1;
            w.metrics.append(m);
            c.widgets.append(w);
        }
        {
            LoadoutWidget w;
            w.key = 4;
            w.widgetType = WidgetType::Readout;
            LoadoutMetricItem m;
            m.metricId = 89; // CPU Power
            m.statId = 1;
            w.metrics.append(m);
            c.widgets.append(w);
        }
        {
            LoadoutWidget w;
            w.key = 5;
            w.widgetType = WidgetType::Readout;
            LoadoutMetricItem m;
            m.metricId = 90; // CPU Temp
            m.statId = 1;
            w.metrics.append(m);
            c.widgets.append(w);
        }
        return c;
    }

    QByteArray ToJson() const {
        QJsonObject root;
        QJsonObject sig;
        sig["code"] = "p2c-cap-load";
        sig["version"] = "1.0.0";
        root["signature"] = sig;

        QJsonArray widgetsArr;
        for (const auto& w : widgets) {
            QJsonObject wObj;
            wObj["key"] = w.key;
            wObj["widgetType"] = static_cast<int>(w.widgetType);
            wObj["graphType"] = static_cast<int>(w.graphType);
            wObj["rangeMin"] = w.rangeMin;
            wObj["rangeMax"] = w.rangeMax;
            wObj["autoScale"] = w.autoScale;

            QJsonArray metricsArr;
            for (const auto& m : w.metrics) {
                QJsonObject mObj;
                QJsonObject metric;
                metric["metricId"] = m.metricId;
                metric["statId"] = m.statId;
                metric["deviceId"] = m.deviceId;
                mObj["metric"] = metric;

                QJsonObject lc;
                lc["r"] = m.lineColor.red();
                lc["g"] = m.lineColor.green();
                lc["b"] = m.lineColor.blue();
                lc["a"] = m.lineColor.alphaF();
                mObj["lineColor"] = lc;

                QJsonObject fc;
                fc["r"] = m.fillColor.red();
                fc["g"] = m.fillColor.green();
                fc["b"] = m.fillColor.blue();
                fc["a"] = m.fillColor.alphaF();
                mObj["fillColor"] = fc;

                mObj["axisAffinity"] = m.axisAffinity;
                metricsArr.append(mObj);
            }
            wObj["metrics"] = metricsArr;
            widgetsArr.append(wObj);
        }
        root["widgets"] = widgetsArr;

        return QJsonDocument(root).toJson(QJsonDocument::Indented);
    }

    bool FromJson(const QByteArray& data) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            return false;
        }
        QJsonObject root = doc.object();
        QJsonArray widgetsArr = root["widgets"].toArray();
        widgets.clear();

        for (int i = 0; i < widgetsArr.size(); ++i) {
            QJsonObject wObj = widgetsArr[i].toObject();
            LoadoutWidget w;
            w.key = wObj["key"].toInt(i);
            w.widgetType = static_cast<WidgetType>(wObj["widgetType"].toInt(1));
            w.graphType = static_cast<GraphType>(wObj["graphType"].toInt(0));
            w.rangeMin = static_cast<float>(wObj["rangeMin"].toDouble(0.0));
            w.rangeMax = static_cast<float>(wObj["rangeMax"].toDouble(50.0));
            w.autoScale = wObj["autoScale"].toBool(true);

            QJsonArray metricsArr = wObj["metrics"].toArray();
            for (int j = 0; j < metricsArr.size(); ++j) {
                QJsonObject mObj = metricsArr[j].toObject();
                QJsonObject metric = mObj["metric"].toObject();
                LoadoutMetricItem m;
                m.metricId = metric["metricId"].toInt(12);
                m.statId = metric["statId"].toInt(1);
                m.deviceId = metric["deviceId"].toInt(0);

                if (mObj.contains("lineColor")) {
                    QJsonObject lc = mObj["lineColor"].toObject();
                    m.lineColor = QColor(lc["r"].toInt(100), lc["g"].toInt(255), lc["b"].toInt(255), static_cast<int>(lc["a"].toDouble(1.0) * 255));
                }
                if (mObj.contains("fillColor")) {
                    QJsonObject fc = mObj["fillColor"].toObject();
                    m.fillColor = QColor(fc["r"].toInt(57), fc["g"].toInt(210), fc["b"].toInt(250), static_cast<int>(fc["a"].toDouble(0.2) * 255));
                }
                m.axisAffinity = mObj["axisAffinity"].toInt(0);
                w.metrics.append(m);
            }
            widgets.append(w);
        }
        return true;
    }
};

struct AppConfig {
    // 1. Process & Targeting
    bool autoTarget = false;
    uint32_t trackedPid = 0;
    QString trackedProcessName = "";

    // 2. Presets
    int selectedPreset = 0; // 0: Basic, 1: Game Experience, 2: GPU Focus, 3: Power/Temp, 1000: Custom

    // 3. Hotkeys
    QString hotkeyOverlay = "Ctrl+Shift+O";
    QString hotkeyPresetCycle = "Ctrl+Shift+P";
    QString hotkeyCapture = "Ctrl+Shift+K";

    // 4. Capture Settings
    bool enableCaptureDuration = false;
    int captureDurationSeconds = 10;
    QString captureDirectory = "";
    bool captureSummaryStats = true;
    bool captureTargetBlockList = true;

    // 5. Overlay Configuration
    bool overlayWindowedMode = false;
    bool overlayHideDuringCapture = true;
    int overlayCorner = 0; // 0: Top-Left, 1: Top-Right, 2: Bottom-Left, 3: Bottom-Right
    int overlayWidth = 400; // 200 - 1920
    double overlayTimeScale = 10.0; // 0.1 - 10.0s
    bool overlayGraphicsScaling = false;
    double overlayScalingFactor = 2.0;
    int overlayDrawRate = 60; // 1 - 120 fps
    QColor overlayBgColor = QColor(50, 57, 91, 220);

    // 6. Data Processing
    int dataPollingRate = 40; // 1 - 240 Hz
    int dataTelemetryPeriod = 100; // 1 - 500 ms
    int dataWindowSize = 1000; // 10 - 5000 ms
    bool dataPerMetricDevice = false;
    QString defaultAdapter = "";

    // Loadout
    LoadoutConfig loadout = LoadoutConfig::MakeDefaultBasic();

    static QString GetConfigFilePath() {
        QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
        if (configDir.isEmpty()) {
            configDir = QDir::homePath() + "/.config";
        }
        configDir += "/gnumon";
        QDir().mkpath(configDir);
        return configDir + "/config.ini";
    }

    static QString GetLoadoutFilePath() {
        QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
        if (configDir.isEmpty()) {
            configDir = QDir::homePath() + "/.config";
        }
        configDir += "/gnumon";
        QDir().mkpath(configDir);
        return configDir + "/custom-auto.json";
    }

    void Load() {
        QString path = GetConfigFilePath();
        QSettings s(path, QSettings::IniFormat);

        s.beginGroup("General");
        autoTarget = s.value("autoTarget", autoTarget).toBool();
        selectedPreset = s.value("selectedPreset", selectedPreset).toInt();
        s.endGroup();

        s.beginGroup("Hotkeys");
        hotkeyOverlay = s.value("overlay", hotkeyOverlay).toString();
        hotkeyPresetCycle = s.value("presetCycle", hotkeyPresetCycle).toString();
        hotkeyCapture = s.value("capture", hotkeyCapture).toString();
        s.endGroup();

        s.beginGroup("Capture");
        enableCaptureDuration = s.value("enableDuration", enableCaptureDuration).toBool();
        captureDurationSeconds = s.value("durationSeconds", captureDurationSeconds).toInt();
        QString defaultDocs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (defaultDocs.isEmpty()) defaultDocs = QDir::homePath() + "/Documents";
        captureDirectory = s.value("directory", defaultDocs + "/gnumon/captures").toString();
        captureSummaryStats = s.value("summaryStats", captureSummaryStats).toBool();
        captureTargetBlockList = s.value("targetBlockList", captureTargetBlockList).toBool();
        s.endGroup();

        s.beginGroup("Overlay");
        overlayWindowedMode = s.value("windowedMode", overlayWindowedMode).toBool();
        overlayHideDuringCapture = s.value("hideDuringCapture", overlayHideDuringCapture).toBool();
        overlayCorner = s.value("inGameHudCorner", overlayCorner).toInt();
        overlayWidth = s.value("width", overlayWidth).toInt();
        overlayTimeScale = s.value("timeScale", overlayTimeScale).toDouble();
        overlayGraphicsScaling = s.value("graphicsScaling", overlayGraphicsScaling).toBool();
        overlayScalingFactor = s.value("scalingFactor", overlayScalingFactor).toDouble();
        overlayDrawRate = s.value("drawRate", overlayDrawRate).toInt();
        QString colStr = s.value("bgColor", overlayBgColor.name(QColor::HexArgb)).toString();
        QColor col(colStr);
        if (col.isValid()) {
            overlayBgColor = col;
        }
        s.endGroup();

        s.beginGroup("Data");
        dataPollingRate = s.value("pollingRate", dataPollingRate).toInt();
        dataTelemetryPeriod = s.value("telemetryPeriod", dataTelemetryPeriod).toInt();
        dataWindowSize = s.value("windowSize", dataWindowSize).toInt();
        dataPerMetricDevice = s.value("perMetricDevice", dataPerMetricDevice).toBool();
        defaultAdapter = s.value("defaultAdapter", defaultAdapter).toString();
        s.endGroup();

        // Load custom loadout if exists
        QFile loadoutFile(GetLoadoutFilePath());
        if (loadoutFile.open(QIODevice::ReadOnly)) {
            loadout.FromJson(loadoutFile.readAll());
        } else {
            // Apply selected preset
            ApplyPreset(selectedPreset);
        }
    }

    void ApplyPreset(int preset) {
        selectedPreset = preset;
        switch (preset) {
        case 0: loadout = LoadoutConfig::MakeDefaultBasic(); break;
        case 1: loadout = LoadoutConfig::MakeGameExperience(); break;
        case 2: loadout = LoadoutConfig::MakeGpuFocus(); break;
        case 3: loadout = LoadoutConfig::MakePowerTemp(); break;
        default: break;
        }
    }

    void Save() const {
        QString path = GetConfigFilePath();
        QSettings s(path, QSettings::IniFormat);

        s.beginGroup("General");
        s.setValue("autoTarget", autoTarget);
        s.setValue("selectedPreset", selectedPreset);
        s.endGroup();

        s.beginGroup("Hotkeys");
        s.setValue("overlay", hotkeyOverlay);
        s.setValue("presetCycle", hotkeyPresetCycle);
        s.setValue("capture", hotkeyCapture);
        s.endGroup();

        s.beginGroup("Capture");
        s.setValue("enableDuration", enableCaptureDuration);
        s.setValue("durationSeconds", captureDurationSeconds);
        s.setValue("directory", captureDirectory);
        s.setValue("summaryStats", captureSummaryStats);
        s.setValue("targetBlockList", captureTargetBlockList);
        s.endGroup();

        s.beginGroup("Overlay");
        s.setValue("windowedMode", overlayWindowedMode);
        s.setValue("hideDuringCapture", overlayHideDuringCapture);
        s.setValue("inGameHudCorner", overlayCorner);
        s.setValue("inGameHudPreset", selectedPreset);
        s.setValue("width", overlayWidth);
        s.setValue("timeScale", overlayTimeScale);
        s.setValue("graphicsScaling", overlayGraphicsScaling);
        s.setValue("scalingFactor", overlayScalingFactor);
        s.setValue("drawRate", overlayDrawRate);
        s.setValue("bgColor", overlayBgColor.name(QColor::HexArgb));
        s.endGroup();

        s.beginGroup("Data");
        s.setValue("pollingRate", dataPollingRate);
        s.setValue("telemetryPeriod", dataTelemetryPeriod);
        s.setValue("windowSize", dataWindowSize);
        s.setValue("perMetricDevice", dataPerMetricDevice);
        s.setValue("defaultAdapter", defaultAdapter);
        s.endGroup();

        s.beginGroup("Loadout");
        s.setValue("widgetCount", loadout.widgets.size());
        for (int i = 0; i < loadout.widgets.size(); ++i) {
            const auto& w = loadout.widgets[i];
            QString pfx = QString("w%1_").arg(i);
            s.setValue(pfx + "type", w.widgetType == WidgetType::Graph ? "graph" : "readout");
            s.setValue(pfx + "graphType", w.graphType == GraphType::Histogram ? "histogram" : "line");
            s.setValue(pfx + "rangeMin", w.rangeMin);
            s.setValue(pfx + "rangeMax", w.rangeMax);
            s.setValue(pfx + "autoScale", w.autoScale);
            s.setValue(pfx + "lineCount", w.metrics.size());
            for (int j = 0; j < w.metrics.size(); ++j) {
                const auto& m = w.metrics[j];
                QString lpfx = QString("%1l%2_").arg(pfx).arg(j);
                s.setValue(lpfx + "metricId", m.metricId);
                s.setValue(lpfx + "statId", m.statId);
                s.setValue(lpfx + "lineColor", m.lineColor.name(QColor::HexArgb));
                s.setValue(lpfx + "fillColor", m.fillColor.name(QColor::HexArgb));
            }
        }
        s.endGroup();

        s.sync();

        // Save loadout
        QFile loadoutFile(GetLoadoutFilePath());
        if (loadoutFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            loadoutFile.write(loadout.ToJson());
        }
    }
};

} // namespace gnumon::gui
