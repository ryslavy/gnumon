#include "AllMetricsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QHeaderView>
#include <QClipboard>
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace gnumon::gui {

namespace {
QString FormatBytes(uint64_t bytes) {
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
    }
    return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 1) + " MB";
}

QWidget* CreateCardWidget(const QString& title, QLabel** outVal, QLabel** outSub, QProgressBar** outBar = nullptr) {
    auto *card = new QWidget();
    card->setStyleSheet("background-color: #1a1e27; border: 1px solid #2d3442; border-radius: 6px; padding: 6px;");
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(4);

    auto *lblTitle = new QLabel(title, card);
    lblTitle->setStyleSheet("color: #78909c; font-size: 11px; font-weight: bold; text-transform: uppercase;");
    layout->addWidget(lblTitle);

    *outVal = new QLabel("--", card);
    (*outVal)->setStyleSheet("color: #00e5ff; font-size: 20px; font-weight: bold;");
    layout->addWidget(*outVal);

    if (outBar) {
        *outBar = new QProgressBar(card);
        (*outBar)->setRange(0, 100);
        (*outBar)->setValue(0);
        (*outBar)->setTextVisible(false);
        (*outBar)->setFixedHeight(5);
        (*outBar)->setStyleSheet(
            "QProgressBar { background-color: #101216; border: none; border-radius: 2px; }"
            "QProgressBar::chunk { background-color: #00e5ff; border-radius: 2px; }"
        );
        layout->addWidget(*outBar);
    }

    *outSub = new QLabel("--", card);
    (*outSub)->setStyleSheet("color: #90a4ae; font-size: 11px;");
    layout->addWidget(*outSub);

    return card;
}

double GetSnapshotMetricValue(int metricId, const PM_FULL_TELEMETRY_SNAPSHOT& s, int extraIdx = 0) {
    switch (metricId) {
    case PM_METRIC_DISPLAYED_FPS: return s.displayedFps;
    case PM_METRIC_PRESENTED_FPS: return s.presentFps;
    case PM_METRIC_APPLICATION_FPS: return s.appFps;
    case PM_METRIC_CPU_FRAME_TIME: return s.cpuFrameTimeMs;
    case PM_METRIC_DISPLAYED_FRAME_TIME: return s.displayedFrameTimeMs;
    case PM_METRIC_PRESENTED_FRAME_TIME: return s.presentedFrameTimeMs;
    case PM_METRIC_IN_PRESENT_API: return s.inPresentApiMs;
    case PM_METRIC_UNTIL_DISPLAYED: return s.untilDisplayedMs;
    case PM_METRIC_PC_LATENCY: return s.pcLatencyMs;
    case PM_METRIC_DISPLAY_LATENCY: return s.displayLatencyMs;
    case PM_METRIC_CLICK_TO_PHOTON_LATENCY: return s.clickToPhotonLatencyMs;
    case PM_METRIC_ANIMATION_ERROR: return s.animationErrorMs;
    case PM_METRIC_GPU_UTILIZATION: return s.gpuUtilizationPercent;
    case PM_METRIC_GPU_POWER: return s.gpuPowerWatts;
    case PM_METRIC_GPU_TEMPERATURE: return (extraIdx == 1) ? s.gpuTemperatureHotspotC : s.gpuTemperatureEdgeC;
    case PM_METRIC_GPU_FREQUENCY: return s.gpuFrequencyMhz;
    case PM_METRIC_GPU_VOLTAGE: return s.gpuVoltageMv;
    case PM_METRIC_GPU_FAN_SPEED: return s.gpuFanSpeedRpm;
    case PM_METRIC_GPU_BUSY: return s.gpuBusyMs;
    case PM_METRIC_GPU_WAIT: return s.gpuWaitMs;
    case PM_METRIC_GPU_MEM_USED: return static_cast<double>(s.vramUsedBytes) / (1024.0 * 1024.0 * 1024.0);
    case PM_METRIC_GPU_MEM_UTILIZATION: return s.vramUtilizationPercent;
    case PM_METRIC_CPU_UTILIZATION: return s.cpuUtilizationPercent;
    case PM_METRIC_CPU_POWER: return s.cpuPackagePowerWatts;
    case PM_METRIC_CPU_TEMPERATURE: return s.cpuTemperatureC;
    case PM_METRIC_CPU_FREQUENCY: return s.cpuFrequencyMhz;
    case PM_METRIC_CPU_BUSY: return s.cpuBusyMs;
    case PM_METRIC_CPU_WAIT: return s.cpuWaitMs;
    default: return 0.0;
    }
}
} // namespace

AllMetricsDialog::AllMetricsDialog(PM_SESSION_HANDLE session, uint32_t processId, QWidget *parent)
    : QDialog(parent), session_(session), processId_(processId)
{
    setWindowTitle("Intel PresentMon 2.x — Full Telemetry Inspector");
    resize(1020, 720);
    setStyleSheet(
        "QDialog { background-color: #13161c; color: #eceff1; font-family: 'Segoe UI', 'Ubuntu', sans-serif; }"
        "QTabWidget::pane { border: 1px solid #2a313d; background-color: #171b22; border-radius: 4px; }"
        "QTabBar::tab { background: #13161c; color: #90a4ae; padding: 8px 16px; border-top-left-radius: 4px; border-top-right-radius: 4px; font-weight: bold; font-size: 12px; }"
        "QTabBar::tab:selected { background: #171b22; color: #00e5ff; border-bottom: 2px solid #00e5ff; }"
        "QTabBar::tab:hover:!selected { color: #e0f7fa; background-color: #1b2029; }"
        "QGroupBox { font-weight: bold; border: 1px solid #2a313d; border-radius: 6px; margin-top: 10px; padding-top: 12px; font-size: 12px; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 12px; padding: 0 4px; color: #00e5ff; }"
        "QPushButton { background-color: #212733; color: #eceff1; border: 1px solid #374254; border-radius: 4px; padding: 6px 14px; font-weight: bold; }"
        "QPushButton:hover { background-color: #2c3444; border-color: #00e5ff; }"
        "QPushButton:pressed { background-color: #00b0ff; color: #101216; }"
        "QTableWidget { background-color: #14171e; gridline-color: #252c38; color: #cfd8dc; border: 1px solid #2a313d; selection-background-color: #005b7f; }"
        "QHeaderView::section { background-color: #1b2029; color: #00e5ff; font-weight: bold; padding: 6px; border: 1px solid #252c38; }"
        "QLineEdit { background-color: #101216; color: #eceff1; border: 1px solid #2a313d; border-radius: 4px; padding: 6px 10px; font-size: 12px; }"
        "QScrollBar:vertical { background: #13161c; width: 10px; }"
        "QScrollBar::handle:vertical { background: #2c3444; border-radius: 5px; min-height: 20px; }"
        "QScrollBar::handle:vertical:hover { background: #00e5ff; }"
    );

    SetupUi();

    refreshTimer_ = new QTimer(this);
    connect(refreshTimer_, &QTimer::timeout, this, &AllMetricsDialog::OnRefreshTimer);
    refreshTimer_->start(100); // 10 Hz refresh for silky live telemetry
}

void AllMetricsDialog::SetTargetProcess(uint32_t pid) {
    processId_ = pid;
    OnRefreshTimer();
}

void AllMetricsDialog::SetupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(14, 14, 14, 14);
    mainLayout->setSpacing(12);

    // --- Header Section ---
    auto *headerWidget = new QWidget(this);
    headerWidget->setStyleSheet("background-color: #171b22; border: 1px solid #2a313d; border-radius: 6px; padding: 4px;");
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(12, 8, 12, 8);

    auto *iconLabel = new QLabel("📊", headerWidget);
    iconLabel->setStyleSheet("font-size: 26px;");
    headerLayout->addWidget(iconLabel);

    auto *titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(2);
    lblProcessTitle_ = new QLabel("Target: Detecting Active Game...", headerWidget);
    lblProcessTitle_->setStyleSheet("font-size: 15px; font-weight: bold; color: #ffffff;");

    auto *subTitleLayout = new QHBoxLayout();
    subTitleLayout->setSpacing(8);

    lblRuntimeBadge_ = new QLabel("Vulkan 1.3", headerWidget);
    lblRuntimeBadge_->setStyleSheet("background-color: #1b3038; color: #00e5ff; padding: 2px 6px; border-radius: 3px; font-size: 11px; font-weight: bold;");

    lblPresentModeBadge_ = new QLabel("Mailbox Flip", headerWidget);
    lblPresentModeBadge_->setStyleSheet("background-color: #1f2f25; color: #00e676; padding: 2px 6px; border-radius: 3px; font-size: 11px; font-weight: bold;");

    lblSwapchain_ = new QLabel("Swapchain: 0x0", headerWidget);
    lblSwapchain_->setStyleSheet("color: #78909c; font-size: 11px; font-family: monospace;");

    subTitleLayout->addWidget(lblRuntimeBadge_);
    subTitleLayout->addWidget(lblPresentModeBadge_);
    subTitleLayout->addWidget(lblSwapchain_);
    subTitleLayout->addStretch();

    titleLayout->addWidget(lblProcessTitle_);
    titleLayout->addLayout(subTitleLayout);
    headerLayout->addLayout(titleLayout);

    headerLayout->addStretch();

    lblStatusBadge_ = new QLabel("● LIVE (10 Hz)", headerWidget);
    lblStatusBadge_->setStyleSheet("color: #00e676; font-weight: bold; font-size: 12px; padding-right: 12px;");
    headerLayout->addWidget(lblStatusBadge_);

    btnPauseResume_ = new QPushButton("Pause", headerWidget);
    connect(btnPauseResume_, &QPushButton::clicked, this, &AllMetricsDialog::OnTogglePause);
    headerLayout->addWidget(btnPauseResume_);

    auto *btnCopy = new QPushButton("Copy JSON", headerWidget);
    connect(btnCopy, &QPushButton::clicked, this, &AllMetricsDialog::OnCopyJson);
    headerLayout->addWidget(btnCopy);

    auto *btnExport = new QPushButton("Export CSV", headerWidget);
    connect(btnExport, &QPushButton::clicked, this, &AllMetricsDialog::OnExportCsv);
    headerLayout->addWidget(btnExport);

    mainLayout->addWidget(headerWidget);

    // --- Tab Widget ---
    auto *tabs = new QTabWidget(this);
    tabWidget_ = tabs;
    SetupOverviewTab(tabs);
    SetupLiveChartTab(tabs);
    SetupPacingTab(tabs);
    SetupLatencyTab(tabs);
    SetupGpuTab(tabs);
    SetupVramTab(tabs);
    SetupCpuTab(tabs);
    SetupPsoTab(tabs);
    SetupDictionaryTab(tabs);
    mainLayout->addWidget(tabs);

    // Bottom Close Bar
    auto *bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch();
    auto *btnClose = new QPushButton("Close Inspector", this);
    btnClose->setStyleSheet("background-color: #263238; color: #eceff1; padding: 6px 20px; font-weight: bold;");
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
    bottomLayout->addWidget(btnClose);
    mainLayout->addLayout(bottomLayout);
}

void AllMetricsDialog::SetupOverviewTab(QTabWidget *tabs) {
    auto *tab = new QWidget();
    auto *layout = new QGridLayout(tab);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(12);

    layout->addWidget(CreateCardWidget("DISPLAYED FPS (Rate)", &lblFpsCurrent_, &lblFpsSub_), 0, 0);
    layout->addWidget(CreateCardWidget("CPU FRAME TIME (Pacing)", &lblFtCurrent_, &lblFtSub_), 0, 1);
    layout->addWidget(CreateCardWidget("PC LATENCY (Responsiveness)", &lblLatencyCurrent_, &lblLatencySub_), 0, 2);
    layout->addWidget(CreateCardWidget("ANIMATION ERROR (Smoothness)", &lblAnimErrCurrent_, &lblAnimErrSub_), 0, 3);

    layout->addWidget(CreateCardWidget("GPU UTILIZATION & POWER", &lblGpuOverview_, &lblGpuOverviewSub_, &barGpuOverview_), 1, 0);
    layout->addWidget(CreateCardWidget("CPU UTILIZATION & POWER", &lblCpuOverview_, &lblCpuOverviewSub_, &barCpuOverview_), 1, 1);
    layout->addWidget(CreateCardWidget("GPU VRAM MEMORY", &lblVramOverview_, &lblVramOverviewSub_, &barVramOverview_), 1, 2);
    layout->addWidget(CreateCardWidget("DISPLAY SYNC & TEARING", &lblSyncOverview_, &lblSyncOverviewSub_), 1, 3);

    tabs->addTab(tab, "Overview Dashboard");
}

void AllMetricsDialog::SetupLiveChartTab(QTabWidget *tabs) {
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    // Top Controls Bar
    auto *controlBar = new QWidget(tab);
    controlBar->setStyleSheet("background-color: #171b22; border: 1px solid #2a313d; border-radius: 6px; padding: 4px;");
    auto *barLayout = new QHBoxLayout(controlBar);
    barLayout->setContentsMargins(10, 6, 10, 6);
    barLayout->setSpacing(10);

    auto *lblP = new QLabel("Primary Metric:", controlBar);
    lblP->setStyleSheet("font-weight: bold; color: #00e5ff;");
    comboPrimary_ = new QComboBox(controlBar);
    comboPrimary_->setStyleSheet("background-color: #101216; color: #00e5ff; border: 1px solid #00e5ff; font-weight: bold; padding: 4px; border-radius: 4px;");

    checkSecondary_ = new QCheckBox("Secondary:", controlBar);
    checkSecondary_->setStyleSheet("font-weight: bold; color: #ff7043;");
    connect(checkSecondary_, &QCheckBox::toggled, this, &AllMetricsDialog::OnSecondaryToggle);

    comboSecondary_ = new QComboBox(controlBar);
    comboSecondary_->setStyleSheet("background-color: #101216; color: #ff7043; border: 1px solid #ff7043; font-weight: bold; padding: 4px; border-radius: 4px;");
    comboSecondary_->setEnabled(false);

    auto *lblW = new QLabel("Window:", controlBar);
    lblW->setStyleSheet("color: #90a4ae; font-weight: bold;");
    comboWindow_ = new QComboBox(controlBar);
    comboWindow_->addItems({"2 Seconds", "5 Seconds", "10 Seconds", "30 Seconds", "60 Seconds"});
    comboWindow_->setCurrentIndex(2); // 10s default
    connect(comboWindow_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AllMetricsDialog::OnTimeWindowChanged);

    auto *btnClear = new QPushButton("Clear History", controlBar);
    btnClear->setStyleSheet("padding: 4px 12px;");
    connect(btnClear, &QPushButton::clicked, this, [this]() {
        if (metricGraph_) metricGraph_->Clear();
    });

    barLayout->addWidget(lblP);
    barLayout->addWidget(comboPrimary_, 1);
    barLayout->addWidget(checkSecondary_);
    barLayout->addWidget(comboSecondary_, 1);
    barLayout->addWidget(lblW);
    barLayout->addWidget(comboWindow_);
    barLayout->addWidget(btnClear);
    layout->addWidget(controlBar);

    // Metric Graph Canvas
    metricGraph_ = new MetricGraphWidget(tab);
    layout->addWidget(metricGraph_, 1);

    // Live Stats Summary Bar below graph
    auto *statsGroup = new QGroupBox("Real-Time Timeline Statistics", tab);
    auto *statsLayout = new QHBoxLayout(statsGroup);
    statsLayout->setContentsMargins(12, 8, 12, 8);

    lblChartCur_ = new QLabel("Current: --", statsGroup);
    lblChartCur_->setStyleSheet("color: #00e5ff; font-weight: bold; font-size: 13px; font-family: monospace;");

    lblChartMin_ = new QLabel("Min: --", statsGroup);
    lblChartMin_->setStyleSheet("color: #90a4ae; font-family: monospace;");

    lblChartMax_ = new QLabel("Max: --", statsGroup);
    lblChartMax_->setStyleSheet("color: #90a4ae; font-family: monospace;");

    lblChartAvg_ = new QLabel("Avg: --", statsGroup);
    lblChartAvg_->setStyleSheet("color: #00e676; font-weight: bold; font-family: monospace;");

    lblChartP99_ = new QLabel("99th% / 1% Low: --", statsGroup);
    lblChartP99_->setStyleSheet("color: #ffd54f; font-weight: bold; font-family: monospace;");

    statsLayout->addWidget(lblChartCur_);
    statsLayout->addStretch();
    statsLayout->addWidget(lblChartMin_);
    statsLayout->addStretch();
    statsLayout->addWidget(lblChartMax_);
    statsLayout->addStretch();
    statsLayout->addWidget(lblChartAvg_);
    statsLayout->addStretch();
    statsLayout->addWidget(lblChartP99_);

    layout->addWidget(statsGroup);

    // Populate Metrics into Combos
    struct ChartMetricOption {
        int id;
        int subIdx;
        QString label;
        QString unit;
    };

    static const std::vector<ChartMetricOption> options = {
        { PM_METRIC_DISPLAYED_FPS, 0, "Displayed FPS", "FPS" },
        { PM_METRIC_CPU_FRAME_TIME, 0, "CPU Frame Time", "ms" },
        { PM_METRIC_PRESENTED_FPS, 0, "Presented FPS", "FPS" },
        { PM_METRIC_APPLICATION_FPS, 0, "Application FPS", "FPS" },
        { PM_METRIC_DISPLAYED_FRAME_TIME, 0, "Displayed Frame Time", "ms" },
        { PM_METRIC_IN_PRESENT_API, 0, "Time in Present API", "ms" },
        { PM_METRIC_UNTIL_DISPLAYED, 0, "Time Until Displayed", "ms" },
        { PM_METRIC_PC_LATENCY, 0, "PC Latency", "ms" },
        { PM_METRIC_DISPLAY_LATENCY, 0, "Display Latency", "ms" },
        { PM_METRIC_CLICK_TO_PHOTON_LATENCY, 0, "Click-To-Photon Latency", "ms" },
        { PM_METRIC_ANIMATION_ERROR, 0, "Animation Error (Smoothness)", "ms" },
        { PM_METRIC_GPU_UTILIZATION, 0, "GPU Utilization", "%" },
        { PM_METRIC_GPU_POWER, 0, "GPU Power", "W" },
        { PM_METRIC_GPU_TEMPERATURE, 0, "GPU Temperature (Edge)", "°C" },
        { PM_METRIC_GPU_TEMPERATURE, 1, "GPU Temperature (Hotspot)", "°C" },
        { PM_METRIC_GPU_FREQUENCY, 0, "GPU Clock Frequency", "MHz" },
        { PM_METRIC_GPU_VOLTAGE, 0, "GPU Core Voltage", "mV" },
        { PM_METRIC_GPU_FAN_SPEED, 0, "GPU Fan Speed", "RPM" },
        { PM_METRIC_GPU_BUSY, 0, "GPU Busy Time", "ms" },
        { PM_METRIC_GPU_WAIT, 0, "GPU Wait Time", "ms" },
        { PM_METRIC_GPU_MEM_USED, 0, "VRAM Used", "GB" },
        { PM_METRIC_GPU_MEM_UTILIZATION, 0, "VRAM Utilization", "%" },
        { PM_METRIC_CPU_UTILIZATION, 0, "CPU Utilization", "%" },
        { PM_METRIC_CPU_POWER, 0, "CPU Package Power", "W" },
        { PM_METRIC_CPU_TEMPERATURE, 0, "CPU Temperature", "°C" },
        { PM_METRIC_CPU_FREQUENCY, 0, "CPU Frequency", "MHz" },
        { PM_METRIC_CPU_BUSY, 0, "CPU Busy Time", "ms" },
        { PM_METRIC_CPU_WAIT, 0, "CPU Wait Time", "ms" },
    };

    for (const auto& opt : options) {
        QVariant data = QVariant::fromValue(opt.id * 10 + opt.subIdx);
        comboPrimary_->addItem(opt.label + " (" + opt.unit + ")", data);
        comboSecondary_->addItem(opt.label + " (" + opt.unit + ")", data);
    }

    comboPrimary_->setCurrentIndex(0); // Displayed FPS
    comboSecondary_->setCurrentIndex(1); // CPU Frame Time

    connect(comboPrimary_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AllMetricsDialog::OnPrimaryMetricChanged);
    connect(comboSecondary_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AllMetricsDialog::OnSecondaryMetricChanged);

    OnPrimaryMetricChanged(0);
    OnSecondaryMetricChanged(1);

    tabs->addTab(tab, "📈 Live Graph Analyzer");
}

void AllMetricsDialog::OnPrimaryMetricChanged(int index) {
    if (!metricGraph_ || index < 0 || !comboPrimary_) return;
    QString text = comboPrimary_->itemText(index);
    QString unit = text.contains('(') ? text.section('(', 1).section(')', 0) : "";
    QString name = text.section('(', 0, 0).trimmed();
    metricGraph_->SetPrimarySeries(name, unit, QColor(0, 229, 255));
}

void AllMetricsDialog::OnSecondaryMetricChanged(int index) {
    if (!metricGraph_ || index < 0 || !comboSecondary_) return;
    QString text = comboSecondary_->itemText(index);
    QString unit = text.contains('(') ? text.section('(', 1).section(')', 0) : "";
    QString name = text.section('(', 0, 0).trimmed();
    metricGraph_->SetSecondarySeries(name, unit, QColor(255, 112, 67));
}

void AllMetricsDialog::OnSecondaryToggle(bool checked) {
    if (comboSecondary_) comboSecondary_->setEnabled(checked);
    if (metricGraph_) metricGraph_->EnableSecondarySeries(checked);
}

void AllMetricsDialog::OnTimeWindowChanged(int index) {
    if (!metricGraph_) return;
    double sec = 10.0;
    if (index == 0) sec = 2.0;
    else if (index == 1) sec = 5.0;
    else if (index == 2) sec = 10.0;
    else if (index == 3) sec = 30.0;
    else if (index == 4) sec = 60.0;
    metricGraph_->SetTimeWindow(sec);
}

void AllMetricsDialog::SelectMetricForChart(int metricId) {
    if (!comboPrimary_) return;
    for (int i = 0; i < comboPrimary_->count(); ++i) {
        int val = comboPrimary_->itemData(i).toInt();
        if (val / 10 == metricId) {
            comboPrimary_->setCurrentIndex(i);
            break;
        }
    }
    if (tabWidget_) {
        tabWidget_->setCurrentIndex(1); // Switch to Live Graph Analyzer tab
    }
}

void AllMetricsDialog::OnTableMetricDoubleClicked(int row, int col) {
    Q_UNUSED(col);
    if (row >= 0 && row < static_cast<int>(metricDefs_.size())) {
        SelectMetricForChart(metricDefs_[row].metricId);
    }
}

void AllMetricsDialog::SetupPacingTab(QTabWidget *tabs) {
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(14, 14, 14, 14);

    auto *gridGroup = new QGroupBox("Frame Timing & Pacing Breakdown (Intel PresentMon 2.x)", tab);
    auto *grid = new QGridLayout(gridGroup);
    grid->setSpacing(10);

    auto addRow = [&](int row, const QString& name, QLabel** outVal, const QString& desc) {
        auto *lblName = new QLabel(name, gridGroup);
        lblName->setStyleSheet("color: #cfd8dc; font-weight: bold;");
        *outVal = new QLabel("--", gridGroup);
        (*outVal)->setStyleSheet("color: #00e5ff; font-weight: bold; font-family: monospace;");
        auto *lblDesc = new QLabel(desc, gridGroup);
        lblDesc->setStyleSheet("color: #78909c; font-size: 11px;");
        grid->addWidget(lblName, row, 0);
        grid->addWidget(*outVal, row, 1);
        grid->addWidget(lblDesc, row, 2);
    };

    addRow(0, "Application Frame Time (CPU):", &lblPacingAppFt_, "Delta from CPU start of frame until CPU started next frame");
    addRow(1, "Displayed Frame Time:", &lblPacingDispFt_, "Time between previous frame display and current frame display");
    addRow(2, "Presented Frame Time:", &lblPacingPresFt_, "Time between successive Present() API invocations");
    addRow(3, "Average Frame Time (Window):", &lblPacingAvgFt_, "Moving average over configured time window");
    addRow(4, "99th Percentile Frame Time:", &lblPacing99pFt_, "Spike detector: 99% of frames were faster than this");
    addRow(5, "Time In Present API:", &lblPacingInApi_, "Duration execution remained blocked inside vkQueuePresentKHR");
    addRow(6, "Time Until Displayed:", &lblPacingUntilDisp_, "Latency from Present() call until swapchain scanout display");
    addRow(7, "Time Between Presents:", &lblPacingBetweenPres_, "Elapsed time between current and previous present call");
    addRow(8, "Flip Delay:", &lblPacingFlipDelay_, "Driver or compositor queue delay added to flip presentation");
    addRow(9, "Dropped Frames:", &lblPacingDropped_, "Frames superseded or dropped prior to screen scanout");
    addRow(10, "Sync Interval:", &lblPacingSyncInt_, "Presentation sync interval requested by application (0=Uncapped)");
    addRow(11, "Allows Tearing:", &lblPacingTearing_, "Whether presentation mode allows immediate tearing flips");

    layout->addWidget(gridGroup);
    layout->addStretch();
    tabs->addTab(tab, "Frame Pacing");
}

void AllMetricsDialog::SetupLatencyTab(QTabWidget *tabs) {
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(14, 14, 14, 14);

    auto *badgeBox = new QHBoxLayout();
    lblLatRatingBadge_ = new QLabel("PC LATENCY: EXCELLENT (<15ms)", tab);
    lblLatRatingBadge_->setStyleSheet("background-color: #1b3824; color: #00e676; padding: 6px 12px; border-radius: 4px; font-weight: bold;");
    lblStutterRatingBadge_ = new QLabel("STUTTER: SMOOTH (<0.5ms error)", tab);
    lblStutterRatingBadge_->setStyleSheet("background-color: #1b3038; color: #00e5ff; padding: 6px 12px; border-radius: 4px; font-weight: bold;");
    badgeBox->addWidget(lblLatRatingBadge_);
    badgeBox->addWidget(lblStutterRatingBadge_);
    badgeBox->addStretch();
    layout->addLayout(badgeBox);

    auto *gridGroup = new QGroupBox("End-to-End Latency & Smoothness Metrics", tab);
    auto *grid = new QGridLayout(gridGroup);
    grid->setSpacing(10);

    auto addRow = [&](int row, const QString& name, QLabel** outVal, const QString& desc) {
        auto *lblName = new QLabel(name, gridGroup);
        lblName->setStyleSheet("color: #cfd8dc; font-weight: bold;");
        *outVal = new QLabel("--", gridGroup);
        (*outVal)->setStyleSheet("color: #00e5ff; font-weight: bold; font-family: monospace;");
        auto *lblDesc = new QLabel(desc, gridGroup);
        lblDesc->setStyleSheet("color: #78909c; font-size: 11px;");
        grid->addWidget(lblName, row, 0);
        grid->addWidget(*outVal, row, 1);
        grid->addWidget(lblDesc, row, 2);
    };

    addRow(0, "PC Latency (Input to Display):", &lblLatPc_, "Elapsed time from CPU input capture to final screen presentation");
    addRow(1, "Display Latency:", &lblLatDisp_, "Latency from frame start until display scanout completed");
    addRow(2, "Click-To-Photon Latency:", &lblLatClick_, "Hardware mouse click to screen presentation latency");
    addRow(3, "All-Input-To-Photon Latency:", &lblLatInput_, "Earliest user interaction that contributed to this frame");
    addRow(4, "Render-Present Latency:", &lblLatRenderPres_, "Time from Present() call until GPU execution finished");
    addRow(5, "Animation Error (Stutter Metric):", &lblLatAnimErr_, "Pacing delta error |DisplayDelta - AppSimulationDelta|");
    addRow(6, "Animation Time:", &lblLatAnimTime_, "Timestamp when simulation loop began animation work");

    layout->addWidget(gridGroup);
    layout->addStretch();
    tabs->addTab(tab, "Latency & Smoothness");
}

void AllMetricsDialog::SetupGpuTab(QTabWidget *tabs) {
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(14, 14, 14, 14);

    auto *gridGroup = new QGroupBox("Graphics Processing Unit (GPU) Telemetry", tab);
    auto *grid = new QGridLayout(gridGroup);
    grid->setSpacing(10);

    auto addField = [&](int row, int col, const QString& name, QLabel** outVal) {
        auto *lblName = new QLabel(name, gridGroup);
        lblName->setStyleSheet("color: #78909c; font-size: 11px; font-weight: bold;");
        *outVal = new QLabel("--", gridGroup);
        (*outVal)->setStyleSheet("color: #ffffff; font-size: 14px; font-weight: bold;");
        grid->addWidget(lblName, row, col);
        grid->addWidget(*outVal, row + 1, col);
    };

    addField(0, 0, "GPU DEVICE NAME", &lblGpuName_);
    addField(0, 1, "CLOCK FREQUENCY", &lblGpuClock_);
    addField(0, 2, "POWER CONSUMPTION", &lblGpuPower_);

    addField(2, 0, "CORE / EDGE TEMP", &lblGpuTempEdge_);
    addField(2, 1, "HOTSPOT TEMP", &lblGpuTempHotspot_);
    addField(2, 2, "VOLTAGE", &lblGpuVoltage_);

    addField(4, 0, "FAN SPEED", &lblGpuFan_);
    addField(4, 1, "GPU WORK DURATION", &lblGpuTime_);
    addField(4, 2, "GPU BUSY / WAIT", &lblGpuBusy_);

    auto *lblUtilTitle = new QLabel("GPU CORE UTILIZATION", gridGroup);
    lblUtilTitle->setStyleSheet("color: #78909c; font-size: 11px; font-weight: bold;");
    lblGpuUtil_ = new QLabel("0.0 %", gridGroup);
    lblGpuUtil_->setStyleSheet("color: #00e5ff; font-weight: bold;");
    barGpuUtil_ = new QProgressBar(gridGroup);
    barGpuUtil_->setRange(0, 100);
    barGpuUtil_->setValue(0);
    barGpuUtil_->setTextVisible(false);
    barGpuUtil_->setFixedHeight(8);

    grid->addWidget(lblUtilTitle, 6, 0);
    grid->addWidget(lblGpuUtil_, 6, 1);
    grid->addWidget(barGpuUtil_, 7, 0, 1, 3);

    layout->addWidget(gridGroup);

    // Limiters Section
    auto *limitGroup = new QGroupBox("Hardware Performance Limiters (Throttling Flags)", tab);
    auto *limitLayout = new QHBoxLayout(limitGroup);
    limitLayout->setSpacing(12);

    auto makeLimiter = [&](const QString& name, QLabel** outLbl) {
        auto *w = new QWidget();
        w->setStyleSheet("background-color: #1a1e27; border: 1px solid #2d3442; border-radius: 4px; padding: 6px;");
        auto *l = new QVBoxLayout(w);
        l->setContentsMargins(6, 4, 6, 4);
        auto *lbl = new QLabel(name, w);
        lbl->setStyleSheet("color: #90a4ae; font-size: 10px; font-weight: bold;");
        *outLbl = new QLabel("NO (Optimal)", w);
        (*outLbl)->setStyleSheet("color: #00e676; font-weight: bold; font-size: 12px;");
        l->addWidget(lbl);
        l->addWidget(*outLbl);
        limitLayout->addWidget(w);
    };

    makeLimiter("POWER LIMIT", &lblLimitPower_);
    makeLimiter("TEMP LIMIT", &lblLimitTemp_);
    makeLimiter("VOLTAGE LIMIT", &lblLimitVoltage_);
    makeLimiter("CURRENT LIMIT", &lblLimitCurrent_);
    makeLimiter("UTILIZATION LIMIT", &lblLimitUtil_);

    layout->addWidget(limitGroup);
    layout->addStretch();
    tabs->addTab(tab, "GPU Telemetry");
}

void AllMetricsDialog::SetupVramTab(QTabWidget *tabs) {
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(14, 14, 14, 14);

    auto *gridGroup = new QGroupBox("Dedicated Video Memory (VRAM) Telemetry", tab);
    auto *grid = new QGridLayout(gridGroup);
    grid->setSpacing(10);

    auto addField = [&](int row, int col, const QString& name, QLabel** outVal) {
        auto *lblName = new QLabel(name, gridGroup);
        lblName->setStyleSheet("color: #78909c; font-size: 11px; font-weight: bold;");
        *outVal = new QLabel("--", gridGroup);
        (*outVal)->setStyleSheet("color: #ffffff; font-size: 14px; font-weight: bold;");
        grid->addWidget(lblName, row, col);
        grid->addWidget(*outVal, row + 1, col);
    };

    addField(0, 0, "VRAM CAPACITY & USED", &lblVramCapacity_);
    addField(0, 1, "VRAM CLOCK SPEED", &lblVramClock_);
    addField(0, 2, "MEMORY BANDWIDTH", &lblVramBandwidth_);

    addField(2, 0, "MEMORY TEMPERATURE", &lblVramTemp_);
    addField(2, 1, "VRAM POWER LIMIT", &lblVramLimitPower_);
    addField(2, 2, "VRAM THERMAL LIMIT", &lblVramLimitTemp_);

    auto *lblBarTitle = new QLabel("VRAM ALLOCATION & UTILIZATION", gridGroup);
    lblBarTitle->setStyleSheet("color: #78909c; font-size: 11px; font-weight: bold;");
    lblVramUtil_ = new QLabel("0.0 %", gridGroup);
    lblVramUtil_->setStyleSheet("color: #00e5ff; font-weight: bold;");
    barVramUtil_ = new QProgressBar(gridGroup);
    barVramUtil_->setRange(0, 100);
    barVramUtil_->setValue(0);
    barVramUtil_->setTextVisible(false);
    barVramUtil_->setFixedHeight(8);

    grid->addWidget(lblBarTitle, 4, 0);
    grid->addWidget(lblVramUtil_, 4, 1);
    grid->addWidget(barVramUtil_, 5, 0, 1, 3);

    layout->addWidget(gridGroup);
    layout->addStretch();
    tabs->addTab(tab, "VRAM & Memory");
}

void AllMetricsDialog::SetupCpuTab(QTabWidget *tabs) {
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(14, 14, 14, 14);

    auto *gridGroup = new QGroupBox("Processor (CPU) Telemetry & Power", tab);
    auto *grid = new QGridLayout(gridGroup);
    grid->setSpacing(10);

    auto addField = [&](int row, int col, const QString& name, QLabel** outVal) {
        auto *lblName = new QLabel(name, gridGroup);
        lblName->setStyleSheet("color: #78909c; font-size: 11px; font-weight: bold;");
        *outVal = new QLabel("--", gridGroup);
        (*outVal)->setStyleSheet("color: #ffffff; font-size: 14px; font-weight: bold;");
        grid->addWidget(lblName, row, col);
        grid->addWidget(*outVal, row + 1, col);
    };

    addField(0, 0, "CPU PROCESSOR MODEL", &lblCpuName_);
    addField(0, 1, "CORES & THREADS", &lblCpuCores_);
    addField(0, 2, "CLOCK FREQUENCY", &lblCpuClock_);

    addField(2, 0, "PACKAGE POWER", &lblCpuPower_);
    addField(2, 1, "PACKAGE TEMPERATURE", &lblCpuTemp_);
    addField(2, 2, "CPU BUSY / WAIT", &lblCpuBusy_);

    auto *lblUtilTitle = new QLabel("TOTAL CPU UTILIZATION", gridGroup);
    lblUtilTitle->setStyleSheet("color: #78909c; font-size: 11px; font-weight: bold;");
    lblCpuUtil_ = new QLabel("0.0 %", gridGroup);
    lblCpuUtil_->setStyleSheet("color: #00e676; font-weight: bold;");
    barCpuUtil_ = new QProgressBar(gridGroup);
    barCpuUtil_->setRange(0, 100);
    barCpuUtil_->setValue(0);
    barCpuUtil_->setTextVisible(false);
    barCpuUtil_->setFixedHeight(8);
    barCpuUtil_->setStyleSheet(
        "QProgressBar { background-color: #101216; border: none; border-radius: 2px; }"
        "QProgressBar::chunk { background-color: #00e676; border-radius: 2px; }"
    );

    grid->addWidget(lblUtilTitle, 4, 0);
    grid->addWidget(lblCpuUtil_, 4, 1);
    grid->addWidget(barCpuUtil_, 5, 0, 1, 3);

    layout->addWidget(gridGroup);

    // Scrollable Per-Core Matrix
    auto *coreGroup = new QGroupBox("Per-Core Real-Time Activity Matrix", tab);
    auto *coreScroll = new QScrollArea(coreGroup);
    coreScroll->setWidgetResizable(true);
    coreScroll->setStyleSheet("background: transparent; border: none;");

    coreContainer_ = new QWidget();
    auto *coreGrid = new QGridLayout(coreContainer_);
    coreGrid->setSpacing(6);

    for (int i = 0; i < 32; ++i) {
        int r = i / 4;
        int c = i % 4;
        auto *cw = new QWidget();
        cw->setStyleSheet("background-color: #1a1e27; border: 1px solid #282f3c; border-radius: 4px; padding: 4px;");
        auto *cl = new QVBoxLayout(cw);
        cl->setContentsMargins(6, 4, 6, 4);
        cl->setSpacing(2);

        auto *lbl = new QLabel(QString("Core %1: 0%").arg(i), cw);
        lbl->setStyleSheet("color: #cfd8dc; font-size: 10px; font-weight: bold;");
        coreLabels_.push_back(lbl);

        auto *bar = new QProgressBar(cw);
        bar->setRange(0, 100);
        bar->setValue(0);
        bar->setTextVisible(false);
        bar->setFixedHeight(4);
        bar->setStyleSheet(
            "QProgressBar { background-color: #101216; border: none; border-radius: 1px; }"
            "QProgressBar::chunk { background-color: #00e676; border-radius: 1px; }"
        );
        coreProgressBars_.push_back(bar);

        cl->addWidget(lbl);
        cl->addWidget(bar);
        coreGrid->addWidget(cw, r, c);
    }

    coreScroll->setWidget(coreContainer_);
    auto *cgl = new QVBoxLayout(coreGroup);
    cgl->addWidget(coreScroll);
    layout->addWidget(coreGroup);

    tabs->addTab(tab, "CPU & Cores");
}

void AllMetricsDialog::SetupPsoTab(QTabWidget *tabs) {
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(14, 14, 14, 14);

    auto *gridGroup = new QGroupBox("Pipeline State Objects (PSO) & Shader Compilation", tab);
    auto *grid = new QGridLayout(gridGroup);
    grid->setSpacing(10);

    auto addField = [&](int row, const QString& name, QLabel** outVal, const QString& desc) {
        auto *lblName = new QLabel(name, gridGroup);
        lblName->setStyleSheet("color: #cfd8dc; font-weight: bold; font-size: 13px;");
        *outVal = new QLabel("--", gridGroup);
        (*outVal)->setStyleSheet("color: #00e5ff; font-weight: bold; font-family: monospace; font-size: 14px;");
        auto *lblDesc = new QLabel(desc, gridGroup);
        lblDesc->setStyleSheet("color: #78909c; font-size: 11px;");
        grid->addWidget(lblName, row, 0);
        grid->addWidget(*outVal, row, 1);
        grid->addWidget(lblDesc, row, 2);
    };

    addField(0, "PSO Compile Count:", &lblPsoCount_, "Number of pipeline state object compiles attributed to this frame period");
    addField(1, "PSO Compile Time:", &lblPsoTime_, "Total cumulative duration spent compiling graphics/compute pipelines");
    addField(2, "PSO Compile Busy Percent:", &lblPsoBusy_, "Fraction of frame time spent actively compiling pipelines (stutter risk indicator)");

    layout->addWidget(gridGroup);
    layout->addStretch();
    tabs->addTab(tab, "PSO & Shaders");
}

void AllMetricsDialog::SetupDictionaryTab(QTabWidget *tabs) {
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    // Search Box
    auto *searchBox = new QHBoxLayout();
    auto *lblFilter = new QLabel("Filter Metrics:", tab);
    lblFilter->setStyleSheet("font-weight: bold; color: #00e5ff;");
    searchFilter_ = new QLineEdit(tab);
    searchFilter_->setPlaceholderText("Search by metric name, category, or description (e.g. latency, fps, temp, vram)...");
    connect(searchFilter_, &QLineEdit::textChanged, this, &AllMetricsDialog::OnSearchFilterChanged);

    searchBox->addWidget(lblFilter);
    searchBox->addWidget(searchFilter_);
    layout->addLayout(searchBox);

    metricsTable_ = new QTableWidget(tab);
    metricsTable_->setColumnCount(5);
    metricsTable_->setHorizontalHeaderLabels({"Metric Identifier", "Live Value", "Unit", "Category", "Description"});
    metricsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    metricsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    metricsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    metricsTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    metricsTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    metricsTable_->verticalHeader()->setVisible(false);
    metricsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    metricsTable_->setAlternatingRowColors(true);
    connect(metricsTable_, &QTableWidget::cellDoubleClicked, this, &AllMetricsDialog::OnTableMetricDoubleClicked);

    // Populate official Intel PresentMon metric dictionary definitions
    metricDefs_ = {
        {PM_METRIC_APPLICATION, "PM_METRIC_APPLICATION", "System", "String", "The name of the process that generated the frame."},
        {PM_METRIC_PROCESS_ID, "PM_METRIC_PROCESS_ID", "System", "Int", "The process ID of the process that generated the frame."},
        {PM_METRIC_SWAP_CHAIN_ADDRESS, "PM_METRIC_SWAP_CHAIN_ADDRESS", "Display", "Hex", "The address of the swap chain used to present the frame."},
        {PM_METRIC_PRESENT_RUNTIME, "PM_METRIC_PRESENT_RUNTIME", "Display", "Enum", "The API used to present the frame (e.g. Vulkan, DXVK, VKD3D)."},
        {PM_METRIC_SYNC_INTERVAL, "PM_METRIC_SYNC_INTERVAL", "Display", "Int", "The sync interval provided by the application when presenting the frame."},
        {PM_METRIC_PRESENT_FLAGS, "PM_METRIC_PRESENT_FLAGS", "Display", "Hex", "The present flags provided by the application when presenting the frame."},
        {PM_METRIC_ALLOWS_TEARING, "PM_METRIC_ALLOWS_TEARING", "Display", "Bool", "1 if partial frames might be displayed on the screen, or 0 if full frames."},
        {PM_METRIC_PRESENT_MODE, "PM_METRIC_PRESENT_MODE", "Display", "Enum", "The presentation mode used by the system (Mailbox, FIFO, Immediate)."},
        {PM_METRIC_FRAME_TYPE, "PM_METRIC_FRAME_TYPE", "Display", "Enum", "Whether frame was rendered by application or generated by driver/SDK."},
        {PM_METRIC_CPU_START_TIME, "PM_METRIC_CPU_START_TIME", "CPU Timing", "ms", "The time the CPU started working on this frame."},
        {PM_METRIC_CPU_START_QPC, "PM_METRIC_CPU_START_QPC", "CPU Timing", "Ticks", "The time the CPU started working on this frame as QPC value."},
        {PM_METRIC_CPU_FRAME_TIME, "PM_METRIC_CPU_FRAME_TIME", "Pacing", "ms", "How long it took from start of frame until next frame started."},
        {PM_METRIC_CPU_BUSY, "PM_METRIC_CPU_BUSY", "CPU Timing", "ms", "How long the CPU spent actively working on this frame."},
        {PM_METRIC_CPU_WAIT, "PM_METRIC_CPU_WAIT", "CPU Timing", "ms", "How long the CPU spent waiting before starting the next frame."},
        {PM_METRIC_GPU_LATENCY, "PM_METRIC_GPU_LATENCY", "GPU Timing", "ms", "How long it took from start of this frame until GPU started working on it."},
        {PM_METRIC_GPU_TIME, "PM_METRIC_GPU_TIME", "GPU Timing", "ms", "The total amount of time that GPU was working on this frame."},
        {PM_METRIC_GPU_BUSY, "PM_METRIC_GPU_BUSY", "GPU Timing", "ms", "How long the GPU was actively executing work from the target process."},
        {PM_METRIC_GPU_WAIT, "PM_METRIC_GPU_WAIT", "GPU Timing", "ms", "How long the GPU was idle while working on this frame."},
        {PM_METRIC_DISPLAY_LATENCY, "PM_METRIC_DISPLAY_LATENCY", "Latency", "ms", "How long it took from start of frame until frame was displayed."},
        {PM_METRIC_DISPLAYED_TIME, "PM_METRIC_DISPLAYED_TIME", "Display", "ms", "How long the frame remained displayed on the screen."},
        {PM_METRIC_ANIMATION_ERROR, "PM_METRIC_ANIMATION_ERROR", "Smoothness", "ms", "The pacing error difference |DisplayDelta - AppDelta|."},
        {PM_METRIC_ANIMATION_TIME, "PM_METRIC_ANIMATION_TIME", "Smoothness", "ms", "The timestamp the CPU started animation work on this frame."},
        {PM_METRIC_CLICK_TO_PHOTON_LATENCY, "PM_METRIC_CLICK_TO_PHOTON_LATENCY", "Latency", "ms", "Duration from earliest mouse click until frame was displayed."},
        {PM_METRIC_ALL_INPUT_TO_PHOTON_LATENCY, "PM_METRIC_ALL_INPUT_TO_PHOTON_LATENCY", "Latency", "ms", "Duration from keyboard/mouse interaction until displayed."},
        {PM_METRIC_PC_LATENCY, "PM_METRIC_PC_LATENCY", "Latency", "ms", "Time between PC receiving input and frame sent to display."},
        {PM_METRIC_DISPLAYED_FPS, "PM_METRIC_DISPLAYED_FPS", "Rate", "FPS", "The rate at which new frames are being displayed on the screen."},
        {PM_METRIC_APPLICATION_FPS, "PM_METRIC_APPLICATION_FPS", "Rate", "FPS", "The rate at which the application is rendering new frames."},
        {PM_METRIC_PRESENTED_FPS, "PM_METRIC_PRESENTED_FPS", "Rate", "FPS", "The rate at which the application is calling Present()."},
        {PM_METRIC_DROPPED_FRAMES, "PM_METRIC_DROPPED_FRAMES", "Display", "Count", "Indicates if the frame was not displayed on the screen."},
        {PM_METRIC_GPU_NAME, "PM_METRIC_GPU_NAME", "GPU", "String", "Device name of the graphics adapter."},
        {PM_METRIC_GPU_VENDOR, "PM_METRIC_GPU_VENDOR", "GPU", "Enum", "Vendor ID of the graphics adapter."},
        {PM_METRIC_GPU_POWER, "PM_METRIC_GPU_POWER", "GPU Power", "W", "Power consumed by the graphics processing unit."},
        {PM_METRIC_GPU_SUSTAINED_POWER_LIMIT, "PM_METRIC_GPU_SUSTAINED_POWER_LIMIT", "GPU Power", "W", "Sustained power limit of the graphics processing unit."},
        {PM_METRIC_GPU_VOLTAGE, "PM_METRIC_GPU_VOLTAGE", "GPU Power", "mV", "Voltage supplied to the graphics adapter core."},
        {PM_METRIC_GPU_FREQUENCY, "PM_METRIC_GPU_FREQUENCY", "GPU Clock", "MHz", "Clock speed of the GPU graphics engines."},
        {PM_METRIC_GPU_TEMPERATURE, "PM_METRIC_GPU_TEMPERATURE", "GPU Thermal", "°C", "Temperature of the graphics processing unit core."},
        {PM_METRIC_GPU_FAN_SPEED, "PM_METRIC_GPU_FAN_SPEED", "GPU Cooling", "RPM", "Rate at which a GPU cooler fan is rotating."},
        {PM_METRIC_GPU_UTILIZATION, "PM_METRIC_GPU_UTILIZATION", "GPU Load", "%", "Amount of GPU graphics processing capacity being used."},
        {PM_METRIC_GPU_RENDER_COMPUTE_UTILIZATION, "PM_METRIC_GPU_RENDER_COMPUTE_UTILIZATION", "GPU Load", "%", "Amount of 3D/Compute processing capacity being used."},
        {PM_METRIC_GPU_POWER_LIMITED, "PM_METRIC_GPU_POWER_LIMITED", "GPU Limit", "Bool", "GPU clock is limited because GPU is at max power limit."},
        {PM_METRIC_GPU_TEMPERATURE_LIMITED, "PM_METRIC_GPU_TEMPERATURE_LIMITED", "GPU Limit", "Bool", "GPU clock is limited because GPU is at max temperature."},
        {PM_METRIC_GPU_VOLTAGE_LIMITED, "PM_METRIC_GPU_VOLTAGE_LIMITED", "GPU Limit", "Bool", "GPU clock is limited because GPU is at max voltage."},
        {PM_METRIC_GPU_CURRENT_LIMITED, "PM_METRIC_GPU_CURRENT_LIMITED", "GPU Limit", "Bool", "GPU clock is limited because GPU is at max current."},
        {PM_METRIC_GPU_UTILIZATION_LIMITED, "PM_METRIC_GPU_UTILIZATION_LIMITED", "GPU Limit", "Bool", "GPU clock is reduced due to low GPU utilization."},
        {PM_METRIC_GPU_MEM_SIZE, "PM_METRIC_GPU_MEM_SIZE", "VRAM", "Bytes", "Total physical size of video memory."},
        {PM_METRIC_GPU_MEM_USED, "PM_METRIC_GPU_MEM_USED", "VRAM", "Bytes", "Amount of allocated/used video memory."},
        {PM_METRIC_GPU_MEM_UTILIZATION, "PM_METRIC_GPU_MEM_UTILIZATION", "VRAM", "%", "Percent of dedicated video memory in use."},
        {PM_METRIC_GPU_MEM_FREQUENCY, "PM_METRIC_GPU_MEM_FREQUENCY", "VRAM", "MHz", "Clock speed of the dedicated GPU memory."},
        {PM_METRIC_GPU_MEM_TEMPERATURE, "PM_METRIC_GPU_MEM_TEMPERATURE", "VRAM", "°C", "Temperature of the dedicated GPU memory modules."},
        {PM_METRIC_GPU_MEM_MAX_BANDWIDTH, "PM_METRIC_GPU_MEM_MAX_BANDWIDTH", "VRAM", "GB/s", "Maximum total theoretical GPU memory bandwidth."},
        {PM_METRIC_CPU_NAME, "PM_METRIC_CPU_NAME", "CPU", "String", "Device model name of the host CPU."},
        {PM_METRIC_CPU_VENDOR, "PM_METRIC_CPU_VENDOR", "CPU", "Enum", "Vendor identifier of the host CPU."},
        {PM_METRIC_CPU_UTILIZATION, "PM_METRIC_CPU_UTILIZATION", "CPU Load", "%", "Amount of host CPU processing capacity being used."},
        {PM_METRIC_CPU_POWER, "PM_METRIC_CPU_POWER", "CPU Power", "W", "Power consumed by the CPU package."},
        {PM_METRIC_CPU_POWER_LIMIT, "PM_METRIC_CPU_POWER_LIMIT", "CPU Power", "W", "Configured power limit of the CPU package."},
        {PM_METRIC_CPU_TEMPERATURE, "PM_METRIC_CPU_TEMPERATURE", "CPU Thermal", "°C", "Average temperature across physical CPU cores."},
        {PM_METRIC_CPU_FREQUENCY, "PM_METRIC_CPU_FREQUENCY", "CPU Clock", "MHz", "Operating clock frequency of the CPU."},
        {PM_METRIC_CPU_CORE_UTILITY, "PM_METRIC_CPU_CORE_UTILITY", "CPU Load", "%", "Amount of processing utility being used per core."},
        {PM_METRIC_CPU_CORE_TEMPERATURE, "PM_METRIC_CPU_CORE_TEMPERATURE", "CPU Thermal", "°C", "Temperature of individual physical CPU cores."},
        {PM_METRIC_IN_PRESENT_API, "PM_METRIC_IN_PRESENT_API", "Timing", "ms", "Duration spent inside the presentation API call."},
        {PM_METRIC_BETWEEN_PRESENTS, "PM_METRIC_BETWEEN_PRESENTS", "Timing", "ms", "Time between this Present() call and previous one."},
        {PM_METRIC_UNTIL_DISPLAYED, "PM_METRIC_UNTIL_DISPLAYED", "Timing", "ms", "Time from Present() call until displayed on screen."},
        {PM_METRIC_RENDER_PRESENT_LATENCY, "PM_METRIC_RENDER_PRESENT_LATENCY", "Timing", "ms", "Time between Present() call and when GPU finished work."},
        {PM_METRIC_DISPLAYED_FRAME_TIME, "PM_METRIC_DISPLAYED_FRAME_TIME", "Pacing", "ms", "Time between display of previous and current frame."},
        {PM_METRIC_PRESENTED_FRAME_TIME, "PM_METRIC_PRESENTED_FRAME_TIME", "Pacing", "ms", "Time between this and previous present invocation."},
        {PM_METRIC_FLIP_DELAY, "PM_METRIC_FLIP_DELAY", "Display", "ms", "Delay added to when the Present() was displayed."},
        {PM_METRIC_PSO_COMPILE_COUNT, "PM_METRIC_PSO_COMPILE_COUNT", "Shaders", "Count", "Number of pipeline state object compiles started."},
        {PM_METRIC_PSO_COMPILE_TIME, "PM_METRIC_PSO_COMPILE_TIME", "Shaders", "ms", "Total time spent compiling pipeline state objects."},
        {PM_METRIC_PSO_COMPILE_BUSY_PERCENT, "PM_METRIC_PSO_COMPILE_BUSY_PERCENT", "Shaders", "%", "Share of frame period spent compiling PSOs."}
    };

    metricsTable_->setRowCount(static_cast<int>(metricDefs_.size()));
    for (size_t i = 0; i < metricDefs_.size(); ++i) {
        metricsTable_->setItem(static_cast<int>(i), 0, new QTableWidgetItem(metricDefs_[i].name));
        metricsTable_->setItem(static_cast<int>(i), 1, new QTableWidgetItem("--"));
        metricsTable_->setItem(static_cast<int>(i), 2, new QTableWidgetItem(metricDefs_[i].unit));
        metricsTable_->setItem(static_cast<int>(i), 3, new QTableWidgetItem(metricDefs_[i].category));
        metricsTable_->setItem(static_cast<int>(i), 4, new QTableWidgetItem(metricDefs_[i].description));
    }

    layout->addWidget(metricsTable_);
    tabs->addTab(tab, "Metrics Dictionary");
}

void AllMetricsDialog::OnSearchFilterChanged(const QString& text) {
    if (!metricsTable_) return;
    QString filter = text.trimmed().toLower();
    for (int r = 0; r < metricsTable_->rowCount(); ++r) {
        if (filter.isEmpty()) {
            metricsTable_->setRowHidden(r, false);
            continue;
        }
        bool match = false;
        for (int c = 0; c < metricsTable_->columnCount(); ++c) {
            auto *item = metricsTable_->item(r, c);
            if (item && item->text().toLower().contains(filter)) {
                match = true;
                break;
            }
        }
        metricsTable_->setRowHidden(r, !match);
    }
}

void AllMetricsDialog::OnTogglePause() {
    isPaused_ = !isPaused_;
    if (isPaused_) {
        btnPauseResume_->setText("Resume");
        lblStatusBadge_->setText("❚❚ PAUSED");
        lblStatusBadge_->setStyleSheet("color: #ffb74d; font-weight: bold; font-size: 12px; padding-right: 12px;");
    } else {
        btnPauseResume_->setText("Pause");
        lblStatusBadge_->setText("● LIVE (10 Hz)");
        lblStatusBadge_->setStyleSheet("color: #00e676; font-weight: bold; font-size: 12px; padding-right: 12px;");
    }
}

void AllMetricsDialog::OnRefreshTimer() {
    if (isPaused_ || !session_) return;

    PM_FULL_TELEMETRY_SNAPSHOT s{};
    if (pmGetFullTelemetrySnapshot(session_, processId_, &s) != PM_STATUS_SUCCESS) {
        return;
    }
    lastSnapshot_ = s;

    // Header Updates
    if (s.processId > 0) {
        lblProcessTitle_->setText(QString("Target: %1 (PID %2)")
            .arg(QString::fromUtf8(s.processName))
            .arg(s.processId));
    } else {
        lblProcessTitle_->setText("Target: Monitoring Active System / Auto-Detecting...");
    }

    lblSwapchain_->setText(QString("Swapchain: 0x%1").arg(s.swapChain, 0, 16));

    QString modeStr = "Mailbox (Uncapped)";
    if (s.presentMode == PM_PRESENT_MODE_HARDWARE_LEGACY_FLIP) modeStr = "Hardware Direct Flip";
    else if (s.presentMode == PM_PRESENT_MODE_COMPOSED_FLIP) modeStr = "Composed Flip";
    lblPresentModeBadge_->setText(modeStr);

    UpdateDashboard(s);
    UpdateLiveChart(s);
    UpdatePacing(s);
    UpdateLatency(s);
    UpdateGpu(s);
    UpdateVram(s);
    UpdateCpu(s);
    UpdatePso(s);
    UpdateDictionary(s);
}

void AllMetricsDialog::UpdateDashboard(const PM_FULL_TELEMETRY_SNAPSHOT& s) {
    lblFpsCurrent_->setText(QString("%1 FPS").arg(s.displayedFps, 0, 'f', 1));
    lblFpsSub_->setText(QString("1%% Low: %1 | Avg: %2 | Min/Max: %3 / %4")
        .arg(s.fps1PercentLow, 0, 'f', 1)
        .arg(s.fpsAvg, 0, 'f', 1)
        .arg(s.fpsMin, 0, 'f', 1)
        .arg(s.fpsMax, 0, 'f', 1));

    lblFtCurrent_->setText(QString("%1 ms").arg(s.cpuFrameTimeMs, 0, 'f', 2));
    lblFtSub_->setText(QString("Avg: %1 ms | 99p: %2 ms | In API: %3 ms")
        .arg(s.cpuFrameTimeAvgMs, 0, 'f', 2)
        .arg(s.cpuFrameTime99pMs, 0, 'f', 2)
        .arg(s.inPresentApiMs, 0, 'f', 2));

    lblLatencyCurrent_->setText(QString("%1 ms").arg(s.pcLatencyMs, 0, 'f', 1));
    lblLatencySub_->setText(QString("Display: %1 ms | Click-To-Photon: %2 ms")
        .arg(s.displayLatencyMs, 0, 'f', 1)
        .arg(s.clickToPhotonLatencyMs, 0, 'f', 1));

    lblAnimErrCurrent_->setText(QString("%1 ms").arg(s.animationErrorMs, 0, 'f', 2));
    lblAnimErrSub_->setText(s.animationErrorMs < 0.5 ? "Status: Fluid pacing (Smooth)" : "Status: Frame pacing variance");

    lblGpuOverview_->setText(QString("%1 %").arg(s.gpuUtilizationPercent, 0, 'f', 0));
    lblGpuOverviewSub_->setText(QString("%1 MHz | %2 W | %3 °C")
        .arg(s.gpuFrequencyMhz, 0, 'f', 0)
        .arg(s.gpuPowerWatts, 0, 'f', 1)
        .arg(s.gpuTemperatureEdgeC, 0, 'f', 0));
    barGpuOverview_->setValue(static_cast<int>(s.gpuUtilizationPercent));

    lblCpuOverview_->setText(QString("%1 %").arg(s.cpuUtilizationPercent, 0, 'f', 0));
    lblCpuOverviewSub_->setText(QString("%1 MHz | %2 W | %3 °C")
        .arg(s.cpuFrequencyMhz, 0, 'f', 0)
        .arg(s.cpuPackagePowerWatts, 0, 'f', 1)
        .arg(s.cpuTemperatureC, 0, 'f', 0));
    barCpuOverview_->setValue(static_cast<int>(s.cpuUtilizationPercent));

    lblVramOverview_->setText(FormatBytes(s.vramUsedBytes));
    lblVramOverviewSub_->setText(QString("Total: %1 | Clock: %2 MHz")
        .arg(FormatBytes(s.vramTotalBytes))
        .arg(s.vramFrequencyMhz, 0, 'f', 0));
    barVramOverview_->setValue(static_cast<int>(s.vramUtilizationPercent));

    lblSyncOverview_->setText(QString("Sync Int: %1").arg(s.syncInterval));
    lblSyncOverviewSub_->setText(QString("Dropped: %1 | Tearing: %2")
        .arg(s.droppedFrames)
        .arg(s.allowsTearing ? "Allowed" : "No"));
}

void AllMetricsDialog::UpdateLiveChart(const PM_FULL_TELEMETRY_SNAPSHOT& s) {
    if (!metricGraph_ || !comboPrimary_) return;

    int pVal = comboPrimary_->currentData().toInt();
    int pMetric = pVal / 10;
    int pSub = pVal % 10;
    double val1 = GetSnapshotMetricValue(pMetric, s, pSub);
    metricGraph_->AddSamplePrimary(val1);

    if (checkSecondary_ && checkSecondary_->isChecked() && comboSecondary_) {
        int sVal = comboSecondary_->currentData().toInt();
        int sMetric = sVal / 10;
        int sSub = sVal % 10;
        double val2 = GetSnapshotMetricValue(sMetric, s, sSub);
        metricGraph_->AddSampleSecondary(val2);
    }

    const auto& prim = metricGraph_->GetPrimarySeries();
    if (lblChartCur_) {
        lblChartCur_->setText(QString("Current: %1 %2").arg(prim.currentValue, 0, 'f', 2).arg(prim.unit));
    }
    if (lblChartMin_) {
        lblChartMin_->setText(QString("Min: %1 %2").arg(prim.minValue, 0, 'f', 2).arg(prim.unit));
    }
    if (lblChartMax_) {
        lblChartMax_->setText(QString("Max: %1 %2").arg(prim.maxValue, 0, 'f', 2).arg(prim.unit));
    }
    if (lblChartAvg_) {
        lblChartAvg_->setText(QString("Avg: %1 %2").arg(prim.avgValue, 0, 'f', 2).arg(prim.unit));
    }
    if (lblChartP99_) {
        lblChartP99_->setText(QString("99th%% / 1%% Low: %1 %2").arg(prim.p99Value, 0, 'f', 2).arg(prim.unit));
    }
}

void AllMetricsDialog::UpdatePacing(const PM_FULL_TELEMETRY_SNAPSHOT& s) {
    lblPacingAppFt_->setText(QString("%1 ms").arg(s.cpuFrameTimeMs, 0, 'f', 3));
    lblPacingDispFt_->setText(QString("%1 ms").arg(s.displayedFrameTimeMs, 0, 'f', 3));
    lblPacingPresFt_->setText(QString("%1 ms").arg(s.presentedFrameTimeMs, 0, 'f', 3));
    lblPacingAvgFt_->setText(QString("%1 ms").arg(s.cpuFrameTimeAvgMs, 0, 'f', 3));
    lblPacing99pFt_->setText(QString("%1 ms").arg(s.cpuFrameTime99pMs, 0, 'f', 3));
    lblPacingInApi_->setText(QString("%1 ms").arg(s.inPresentApiMs, 0, 'f', 3));
    lblPacingUntilDisp_->setText(QString("%1 ms").arg(s.untilDisplayedMs, 0, 'f', 3));
    lblPacingBetweenPres_->setText(QString("%1 ms").arg(s.betweenPresentsMs, 0, 'f', 3));
    lblPacingFlipDelay_->setText(QString("%1 ms").arg(s.flipDelayMs, 0, 'f', 3));
    lblPacingDropped_->setText(QString::number(s.droppedFrames));
    lblPacingSyncInt_->setText(QString::number(s.syncInterval));
    lblPacingTearing_->setText(s.allowsTearing ? "Yes (Immediate)" : "No (VSync / Mailbox)");
}

void AllMetricsDialog::UpdateLatency(const PM_FULL_TELEMETRY_SNAPSHOT& s) {
    lblLatPc_->setText(QString("%1 ms").arg(s.pcLatencyMs, 0, 'f', 2));
    lblLatDisp_->setText(QString("%1 ms").arg(s.displayLatencyMs, 0, 'f', 2));
    lblLatClick_->setText(QString("%1 ms").arg(s.clickToPhotonLatencyMs, 0, 'f', 2));
    lblLatInput_->setText(QString("%1 ms").arg(s.allInputLatencyMs, 0, 'f', 2));
    lblLatRenderPres_->setText(QString("%1 ms").arg(s.renderPresentLatencyMs, 0, 'f', 2));
    lblLatAnimErr_->setText(QString("%1 ms").arg(s.animationErrorMs, 0, 'f', 3));
    lblLatAnimTime_->setText(QString("%1 ms").arg(s.animationTimeMs, 0, 'f', 2));

    if (s.pcLatencyMs > 0.0 && s.pcLatencyMs <= 20.0) {
        lblLatRatingBadge_->setText(QString("PC LATENCY: EXCELLENT (%1 ms)").arg(s.pcLatencyMs, 0, 'f', 1));
        lblLatRatingBadge_->setStyleSheet("background-color: #1b3824; color: #00e676; padding: 6px 12px; border-radius: 4px; font-weight: bold;");
    } else if (s.pcLatencyMs <= 35.0) {
        lblLatRatingBadge_->setText(QString("PC LATENCY: GOOD (%1 ms)").arg(s.pcLatencyMs, 0, 'f', 1));
        lblLatRatingBadge_->setStyleSheet("background-color: #1b3038; color: #00e5ff; padding: 6px 12px; border-radius: 4px; font-weight: bold;");
    } else {
        lblLatRatingBadge_->setText(QString("PC LATENCY: ELEVATED (%1 ms)").arg(s.pcLatencyMs, 0, 'f', 1));
        lblLatRatingBadge_->setStyleSheet("background-color: #3e2723; color: #ffab91; padding: 6px 12px; border-radius: 4px; font-weight: bold;");
    }

    if (s.animationErrorMs < 0.5) {
        lblStutterRatingBadge_->setText("STUTTER: SMOOTH (<0.5ms variance)");
        lblStutterRatingBadge_->setStyleSheet("background-color: #1b3824; color: #00e676; padding: 6px 12px; border-radius: 4px; font-weight: bold;");
    } else {
        lblStutterRatingBadge_->setText(QString("STUTTER: JITTER DETECTED (%1 ms)").arg(s.animationErrorMs, 0, 'f', 2));
        lblStutterRatingBadge_->setStyleSheet("background-color: #382d1b; color: #ffd54f; padding: 6px 12px; border-radius: 4px; font-weight: bold;");
    }
}

void AllMetricsDialog::UpdateGpu(const PM_FULL_TELEMETRY_SNAPSHOT& s) {
    lblGpuName_->setText(QString::fromUtf8(s.gpuName));
    lblGpuClock_->setText(QString("%1 MHz (Eff: %2 MHz)").arg(s.gpuFrequencyMhz, 0, 'f', 0).arg(s.gpuEffectiveFrequencyMhz, 0, 'f', 0));
    lblGpuPower_->setText(QString("%1 W / %2 W limit").arg(s.gpuPowerWatts, 0, 'f', 1).arg(s.gpuSustainedPowerLimitWatts, 0, 'f', 0));
    lblGpuTempEdge_->setText(QString("%1 °C").arg(s.gpuTemperatureEdgeC, 0, 'f', 1));
    lblGpuTempHotspot_->setText(QString("%1 °C").arg(s.gpuTemperatureHotspotC, 0, 'f', 1));
    lblGpuVoltage_->setText(QString("%1 mV").arg(s.gpuVoltageMv, 0, 'f', 0));
    lblGpuFan_->setText(QString("%1 RPM (%2 %)").arg(s.gpuFanSpeedRpm, 0, 'f', 0).arg(s.gpuFanSpeedPercent, 0, 'f', 0));
    lblGpuTime_->setText(QString("%1 ms").arg(s.gpuTimeMs, 0, 'f', 2));
    lblGpuBusy_->setText(QString("%1 ms / %2 ms wait").arg(s.gpuBusyMs, 0, 'f', 2).arg(s.gpuWaitMs, 0, 'f', 2));

    lblGpuUtil_->setText(QString("%1 % (3D: %2 %)").arg(s.gpuUtilizationPercent, 0, 'f', 1).arg(s.gpuRenderComputeUtilizationPercent, 0, 'f', 1));
    barGpuUtil_->setValue(static_cast<int>(s.gpuUtilizationPercent));

    auto setLimiter = [](QLabel* lbl, uint32_t active) {
        if (active) {
            lbl->setText("YES (THROTTLING)");
            lbl->setStyleSheet("color: #ff5252; font-weight: bold; font-size: 12px;");
        } else {
            lbl->setText("NO (Optimal)");
            lbl->setStyleSheet("color: #00e676; font-weight: bold; font-size: 12px;");
        }
    };
    setLimiter(lblLimitPower_, s.gpuPowerLimited);
    setLimiter(lblLimitTemp_, s.gpuTemperatureLimited);
    setLimiter(lblLimitVoltage_, s.gpuVoltageLimited);
    setLimiter(lblLimitCurrent_, s.gpuCurrentLimited);
    setLimiter(lblLimitUtil_, s.gpuUtilizationLimited);
}

void AllMetricsDialog::UpdateVram(const PM_FULL_TELEMETRY_SNAPSHOT& s) {
    lblVramCapacity_->setText(QString("%1 / %2").arg(FormatBytes(s.vramUsedBytes)).arg(FormatBytes(s.vramTotalBytes)));
    lblVramClock_->setText(QString("%1 MHz").arg(s.vramFrequencyMhz, 0, 'f', 0));
    lblVramBandwidth_->setText(QString("%1 GB/s (Max: %2 GB/s)").arg(s.vramEffectiveBandwidthGbs, 0, 'f', 1).arg(s.vramMaxBandwidthGbs, 0, 'f', 1));
    lblVramTemp_->setText(QString("%1 °C").arg(s.gpuTemperatureVramC, 0, 'f', 1));

    lblVramUtil_->setText(QString("%1 %").arg(s.vramUtilizationPercent, 0, 'f', 1));
    barVramUtil_->setValue(static_cast<int>(s.vramUtilizationPercent));

    lblVramLimitPower_->setText(s.vramPowerLimited ? "YES (Power Limited)" : "NO (OK)");
    lblVramLimitPower_->setStyleSheet(s.vramPowerLimited ? "color: #ff5252; font-weight: bold;" : "color: #00e676; font-weight: bold;");

    lblVramLimitTemp_->setText(s.vramTemperatureLimited ? "YES (Thermal Throttled)" : "NO (OK)");
    lblVramLimitTemp_->setStyleSheet(s.vramTemperatureLimited ? "color: #ff5252; font-weight: bold;" : "color: #00e676; font-weight: bold;");
}

void AllMetricsDialog::UpdateCpu(const PM_FULL_TELEMETRY_SNAPSHOT& s) {
    lblCpuName_->setText(QString::fromUtf8(s.cpuName));
    lblCpuCores_->setText(QString("%1 Physical / SMT Cores").arg(s.cpuCoreCount));
    lblCpuClock_->setText(QString("%1 MHz").arg(s.cpuFrequencyMhz, 0, 'f', 0));
    lblCpuPower_->setText(QString("%1 W (Limit: %2 W)").arg(s.cpuPackagePowerWatts, 0, 'f', 1).arg(s.cpuPowerLimitWatts, 0, 'f', 0));
    lblCpuTemp_->setText(QString("%1 °C").arg(s.cpuTemperatureC, 0, 'f', 1));
    lblCpuBusy_->setText(QString("%1 ms / %2 ms wait").arg(s.cpuBusyMs, 0, 'f', 2).arg(s.cpuWaitMs, 0, 'f', 2));

    lblCpuUtil_->setText(QString("%1 %").arg(s.cpuUtilizationPercent, 0, 'f', 1));
    barCpuUtil_->setValue(static_cast<int>(s.cpuUtilizationPercent));

    for (size_t i = 0; i < coreLabels_.size(); ++i) {
        if (i < 128) {
            double u = s.perCoreUtilization[i];
            double t = s.perCoreTemperature[i];
            coreLabels_[i]->setText(QString("Core %1: %2%% (%3°C)").arg(i).arg(u, 0, 'f', 0).arg(t > 0 ? QString::number(t, 'f', 0) : "--"));
            coreProgressBars_[i]->setValue(static_cast<int>(u));
        }
    }
}

void AllMetricsDialog::UpdatePso(const PM_FULL_TELEMETRY_SNAPSHOT& s) {
    lblPsoCount_->setText(QString::number(s.psoCompileCount));
    lblPsoTime_->setText(QString("%1 ms").arg(s.psoCompileTimeMs, 0, 'f', 2));
    lblPsoBusy_->setText(QString("%1 %").arg(s.psoCompileBusyPercent, 0, 'f', 1));
}

void AllMetricsDialog::UpdateDictionary(const PM_FULL_TELEMETRY_SNAPSHOT& s) {
    for (int i = 0; i < metricsTable_->rowCount(); ++i) {
        auto *item = metricsTable_->item(i, 0);
        if (!item) continue;
        QString name = item->text();

        QString val = "--";
        if (name == "PM_METRIC_APPLICATION") val = QString::fromUtf8(s.processName);
        else if (name == "PM_METRIC_PROCESS_ID") val = QString::number(s.processId);
        else if (name == "PM_METRIC_SWAP_CHAIN_ADDRESS") val = QString("0x%1").arg(s.swapChain, 0, 16);
        else if (name == "PM_METRIC_PRESENT_RUNTIME") val = "Vulkan";
        else if (name == "PM_METRIC_SYNC_INTERVAL") val = QString::number(s.syncInterval);
        else if (name == "PM_METRIC_ALLOWS_TEARING") val = s.allowsTearing ? "1 (True)" : "0 (False)";
        else if (name == "PM_METRIC_PRESENT_MODE") val = "Mailbox";
        else if (name == "PM_METRIC_FRAME_TYPE") val = (s.frameType == 2) ? "Application" : "Generated";
        else if (name == "PM_METRIC_CPU_FRAME_TIME") val = QString::number(s.cpuFrameTimeMs, 'f', 3);
        else if (name == "PM_METRIC_CPU_BUSY") val = QString::number(s.cpuBusyMs, 'f', 3);
        else if (name == "PM_METRIC_CPU_WAIT") val = QString::number(s.cpuWaitMs, 'f', 3);
        else if (name == "PM_METRIC_GPU_TIME") val = QString::number(s.gpuTimeMs, 'f', 3);
        else if (name == "PM_METRIC_GPU_BUSY") val = QString::number(s.gpuBusyMs, 'f', 3);
        else if (name == "PM_METRIC_GPU_WAIT") val = QString::number(s.gpuWaitMs, 'f', 3);
        else if (name == "PM_METRIC_DISPLAY_LATENCY") val = QString::number(s.displayLatencyMs, 'f', 2);
        else if (name == "PM_METRIC_ANIMATION_ERROR") val = QString::number(s.animationErrorMs, 'f', 3);
        else if (name == "PM_METRIC_CLICK_TO_PHOTON_LATENCY") val = QString::number(s.clickToPhotonLatencyMs, 'f', 2);
        else if (name == "PM_METRIC_ALL_INPUT_TO_PHOTON_LATENCY") val = QString::number(s.allInputLatencyMs, 'f', 2);
        else if (name == "PM_METRIC_PC_LATENCY") val = QString::number(s.pcLatencyMs, 'f', 2);
        else if (name == "PM_METRIC_DISPLAYED_FPS") val = QString::number(s.displayedFps, 'f', 1);
        else if (name == "PM_METRIC_APPLICATION_FPS") val = QString::number(s.appFps, 'f', 1);
        else if (name == "PM_METRIC_PRESENTED_FPS") val = QString::number(s.presentFps, 'f', 1);
        else if (name == "PM_METRIC_DROPPED_FRAMES") val = QString::number(s.droppedFrames);
        else if (name == "PM_METRIC_GPU_NAME") val = QString::fromUtf8(s.gpuName);
        else if (name == "PM_METRIC_GPU_POWER") val = QString::number(s.gpuPowerWatts, 'f', 1);
        else if (name == "PM_METRIC_GPU_SUSTAINED_POWER_LIMIT") val = QString::number(s.gpuSustainedPowerLimitWatts, 'f', 0);
        else if (name == "PM_METRIC_GPU_VOLTAGE") val = QString::number(s.gpuVoltageMv, 'f', 0);
        else if (name == "PM_METRIC_GPU_FREQUENCY") val = QString::number(s.gpuFrequencyMhz, 'f', 0);
        else if (name == "PM_METRIC_GPU_TEMPERATURE") val = QString::number(s.gpuTemperatureEdgeC, 'f', 1);
        else if (name == "PM_METRIC_GPU_FAN_SPEED") val = QString::number(s.gpuFanSpeedRpm, 'f', 0);
        else if (name == "PM_METRIC_GPU_UTILIZATION") val = QString::number(s.gpuUtilizationPercent, 'f', 1);
        else if (name == "PM_METRIC_GPU_RENDER_COMPUTE_UTILIZATION") val = QString::number(s.gpuRenderComputeUtilizationPercent, 'f', 1);
        else if (name == "PM_METRIC_GPU_POWER_LIMITED") val = s.gpuPowerLimited ? "1 (Active)" : "0 (False)";
        else if (name == "PM_METRIC_GPU_TEMPERATURE_LIMITED") val = s.gpuTemperatureLimited ? "1 (Active)" : "0 (False)";
        else if (name == "PM_METRIC_GPU_VOLTAGE_LIMITED") val = s.gpuVoltageLimited ? "1 (Active)" : "0 (False)";
        else if (name == "PM_METRIC_GPU_CURRENT_LIMITED") val = s.gpuCurrentLimited ? "1 (Active)" : "0 (False)";
        else if (name == "PM_METRIC_GPU_UTILIZATION_LIMITED") val = s.gpuUtilizationLimited ? "1 (Active)" : "0 (False)";
        else if (name == "PM_METRIC_GPU_MEM_SIZE") val = FormatBytes(s.vramTotalBytes);
        else if (name == "PM_METRIC_GPU_MEM_USED") val = FormatBytes(s.vramUsedBytes);
        else if (name == "PM_METRIC_GPU_MEM_UTILIZATION") val = QString::number(s.vramUtilizationPercent, 'f', 1);
        else if (name == "PM_METRIC_GPU_MEM_FREQUENCY") val = QString::number(s.vramFrequencyMhz, 'f', 0);
        else if (name == "PM_METRIC_GPU_MEM_TEMPERATURE") val = QString::number(s.gpuTemperatureVramC, 'f', 1);
        else if (name == "PM_METRIC_GPU_MEM_MAX_BANDWIDTH") val = QString::number(s.vramMaxBandwidthGbs, 'f', 1);
        else if (name == "PM_METRIC_CPU_NAME") val = QString::fromUtf8(s.cpuName);
        else if (name == "PM_METRIC_CPU_UTILIZATION") val = QString::number(s.cpuUtilizationPercent, 'f', 1);
        else if (name == "PM_METRIC_CPU_POWER") val = QString::number(s.cpuPackagePowerWatts, 'f', 1);
        else if (name == "PM_METRIC_CPU_POWER_LIMIT") val = QString::number(s.cpuPowerLimitWatts, 'f', 0);
        else if (name == "PM_METRIC_CPU_TEMPERATURE") val = QString::number(s.cpuTemperatureC, 'f', 1);
        else if (name == "PM_METRIC_CPU_FREQUENCY") val = QString::number(s.cpuFrequencyMhz, 'f', 0);
        else if (name == "PM_METRIC_IN_PRESENT_API") val = QString::number(s.inPresentApiMs, 'f', 3);
        else if (name == "PM_METRIC_BETWEEN_PRESENTS") val = QString::number(s.betweenPresentsMs, 'f', 3);
        else if (name == "PM_METRIC_UNTIL_DISPLAYED") val = QString::number(s.untilDisplayedMs, 'f', 3);
        else if (name == "PM_METRIC_RENDER_PRESENT_LATENCY") val = QString::number(s.renderPresentLatencyMs, 'f', 3);
        else if (name == "PM_METRIC_DISPLAYED_FRAME_TIME") val = QString::number(s.displayedFrameTimeMs, 'f', 3);
        else if (name == "PM_METRIC_PRESENTED_FRAME_TIME") val = QString::number(s.presentedFrameTimeMs, 'f', 3);
        else if (name == "PM_METRIC_FLIP_DELAY") val = QString::number(s.flipDelayMs, 'f', 3);
        else if (name == "PM_METRIC_PSO_COMPILE_COUNT") val = QString::number(s.psoCompileCount);
        else if (name == "PM_METRIC_PSO_COMPILE_TIME") val = QString::number(s.psoCompileTimeMs, 'f', 2);
        else if (name == "PM_METRIC_PSO_COMPILE_BUSY_PERCENT") val = QString::number(s.psoCompileBusyPercent, 'f', 1);

        auto *valItem = metricsTable_->item(i, 1);
        if (valItem) {
            valItem->setText(val);
        }
    }
}

void AllMetricsDialog::OnCopyJson() {
    std::ostringstream ss;
    ss << "{\n"
       << "  \"timestamp\": \"" << QDateTime::currentDateTime().toString(Qt::ISODate).toStdString() << "\",\n"
       << "  \"process\": {\n"
       << "    \"name\": \"" << lastSnapshot_.processName << "\",\n"
       << "    \"pid\": " << lastSnapshot_.processId << ",\n"
       << "    \"swapchain\": \"0x" << std::hex << lastSnapshot_.swapChain << std::dec << "\",\n"
       << "    \"runtime\": \"Vulkan\"\n"
       << "  },\n"
       << "  \"framerate\": {\n"
       << "    \"displayed_fps\": " << lastSnapshot_.displayedFps << ",\n"
       << "    \"present_fps\": " << lastSnapshot_.presentFps << ",\n"
       << "    \"low_1pct_fps\": " << lastSnapshot_.fps1PercentLow << ",\n"
       << "    \"avg_fps\": " << lastSnapshot_.fpsAvg << "\n"
       << "  },\n"
       << "  \"frametime_ms\": {\n"
       << "    \"cpu_frametime\": " << lastSnapshot_.cpuFrameTimeMs << ",\n"
       << "    \"displayed_frametime\": " << lastSnapshot_.displayedFrameTimeMs << ",\n"
       << "    \"in_present_api\": " << lastSnapshot_.inPresentApiMs << "\n"
       << "  },\n"
       << "  \"latency_ms\": {\n"
       << "    \"pc_latency\": " << lastSnapshot_.pcLatencyMs << ",\n"
       << "    \"display_latency\": " << lastSnapshot_.displayLatencyMs << ",\n"
       << "    \"animation_error\": " << lastSnapshot_.animationErrorMs << "\n"
       << "  },\n"
       << "  \"gpu\": {\n"
       << "    \"name\": \"" << lastSnapshot_.gpuName << "\",\n"
       << "    \"utilization_pct\": " << lastSnapshot_.gpuUtilizationPercent << ",\n"
       << "    \"clock_mhz\": " << lastSnapshot_.gpuFrequencyMhz << ",\n"
       << "    \"power_watts\": " << lastSnapshot_.gpuPowerWatts << ",\n"
       << "    \"temp_edge_c\": " << lastSnapshot_.gpuTemperatureEdgeC << ",\n"
       << "    \"temp_hotspot_c\": " << lastSnapshot_.gpuTemperatureHotspotC << ",\n"
       << "    \"vram_used_bytes\": " << lastSnapshot_.vramUsedBytes << ",\n"
       << "    \"vram_total_bytes\": " << lastSnapshot_.vramTotalBytes << "\n"
       << "  },\n"
       << "  \"cpu\": {\n"
       << "    \"name\": \"" << lastSnapshot_.cpuName << "\",\n"
       << "    \"utilization_pct\": " << lastSnapshot_.cpuUtilizationPercent << ",\n"
       << "    \"power_watts\": " << lastSnapshot_.cpuPackagePowerWatts << ",\n"
       << "    \"temp_c\": " << lastSnapshot_.cpuTemperatureC << ",\n"
       << "    \"clock_mhz\": " << lastSnapshot_.cpuFrequencyMhz << "\n"
       << "  }\n"
       << "}";

    QApplication::clipboard()->setText(QString::fromStdString(ss.str()));
    QMessageBox::information(this, "Copied to Clipboard", "Telemetry snapshot formatted as JSON copied to clipboard!");
}

void AllMetricsDialog::OnExportCsv() {
    QString filename = QFileDialog::getSaveFileName(this, "Export All Metrics Snapshot CSV",
        QString("presentmon_metrics_%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")),
        "CSV Files (*.csv)");
    if (filename.isEmpty()) return;

    std::ofstream f(filename.toStdString());
    if (!f.is_open()) {
        QMessageBox::critical(this, "Export Error", "Unable to open file for writing.");
        return;
    }

    f << "Metric,Value,Unit,Category,Description\n";
    for (int r = 0; r < metricsTable_->rowCount(); ++r) {
        f << "\"" << metricsTable_->item(r, 0)->text().toStdString() << "\","
          << "\"" << metricsTable_->item(r, 1)->text().toStdString() << "\","
          << "\"" << metricsTable_->item(r, 2)->text().toStdString() << "\","
          << "\"" << metricsTable_->item(r, 3)->text().toStdString() << "\","
          << "\"" << metricsTable_->item(r, 4)->text().toStdString() << "\"\n";
    }
    f.close();
    QMessageBox::information(this, "Export Successful", QString("All PresentMon metrics exported to:\n%1").arg(filename));
}

} // namespace gnumon::gui
