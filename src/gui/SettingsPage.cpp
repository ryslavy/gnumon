#include "SettingsPage.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QFileDialog>
#include <QProcess>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QUrl>
#include <sys/utsname.h>

namespace gnumon::gui {

static QString FindSetupScript() {
    QString appDir = QCoreApplication::applicationDirPath();
    QString envAppDir = qgetenv("APPDIR");
    QStringList candidates = {
        appDir + "/setup-service.sh",
        envAppDir + "/usr/bin/setup-service.sh",
        appDir + "/../scripts/setup-service.sh",
        appDir + "/../../scripts/setup-service.sh",
        QDir::homePath() + "/.local/bin/setup-service.sh",
        "/usr/local/bin/setup-service.sh",
        "/usr/bin/setup-service.sh"
    };
    for (const auto& path : candidates) {
        if (!path.isEmpty() && QFileInfo::exists(path)) {
            return QFileInfo(path).canonicalFilePath();
        }
    }
    return "setup-service.sh";
}

SettingsPage::SettingsPage(AppConfig *config, QWidget *parent)
    : QWidget(parent), config_(config)
{
    SetupUi();
    ReloadFromConfig();

    serviceTimer_ = new QTimer(this);
    connect(serviceTimer_, &QTimer::timeout, this, &SettingsPage::RefreshServiceStatus);
    serviceTimer_->start(3000);
    RefreshServiceStatus();
}

void SettingsPage::SetupUi() {
    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    setStyleSheet(
        "SettingsPage {"
        "  background-color: #0b0c10;"
        "}"
        "QLabel {"
        "  color: #e0e0e6;"
        "}"
        "QSlider::groove:horizontal {"
        "  border: none;"
        "  height: 4px;"
        "  background: #2b2e3b;"
        "  border-radius: 2px;"
        "}"
        "QSlider::sub-page:horizontal {"
        "  background: #1976d2;"
        "  border-radius: 2px;"
        "}"
        "QSlider::handle:horizontal {"
        "  background: #64b5f6;"
        "  border: none;"
        "  width: 14px;"
        "  margin-top: -5px;"
        "  margin-bottom: -5px;"
        "  border-radius: 7px;"
        "}"
        "QComboBox, QLineEdit {"
        "  background-color: #12131a;"
        "  border: 1px solid #363948;"
        "  border-radius: 4px;"
        "  padding: 6px 10px;"
        "  color: #e0e0e5;"
        "}"
    );

    // Left Navigation Drawer
    mainLayout->addWidget(CreateSidebar());

    // Right Content Area
    subStack_ = new QStackedWidget(this);
    subStack_->addWidget(CreateOverlayPage());
    subStack_->addWidget(CreateDataPage());
    subStack_->addWidget(CreateCapturePage());
    subStack_->addWidget(CreateLoggingPage());
    subStack_->addWidget(CreateOtherPage());
    subStack_->addWidget(CreateAboutPage());

    mainLayout->addWidget(subStack_, 1);
}

QWidget* SettingsPage::CreateSidebar() {
    auto *sidebar = new QWidget(this);
    sidebar->setFixedWidth(180);
    sidebar->setStyleSheet("background-color: #030308; border-right: 1px solid #1a1b24;");

    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(0, 16, 0, 16);
    layout->setSpacing(4);

    // < TOP button
    auto *btnTop = new QPushButton("< TOP", sidebar);
    btnTop->setStyleSheet(
        "QPushButton {"
        "  background: transparent;"
        "  color: #f0f0f5;"
        "  font-size: 16px;"
        "  font-weight: bold;"
        "  text-align: left;"
        "  padding: 8px 16px;"
        "  border: none;"
        "}"
        "QPushButton:hover {"
        "  color: #64b5f6;"
        "}"
    );
    btnTop->setCursor(Qt::PointingHandCursor);
    connect(btnTop, &QPushButton::clicked, this, &SettingsPage::topRequested);
    layout->addWidget(btnTop);
    layout->addSpacing(12);

    const QStringList tabs = {"Overlay", "Data", "Capture", "Logging", "Other", "About"};
    for (int i = 0; i < tabs.size(); ++i) {
        auto *btn = new QPushButton(tabs[i], sidebar);
        btn->setStyleSheet(
            "QPushButton {"
            "  background: transparent;"
            "  color: #a0a4b8;"
            "  font-size: 14px;"
            "  text-align: left;"
            "  padding: 10px 20px;"
            "  border: none;"
            "  border-radius: 0px;"
            "}"
            "QPushButton:hover {"
            "  background-color: #0d0f17;"
            "  color: #ffffff;"
            "}"
        );
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            OnNavClicked(i);
        });
        navButtons_.append(btn);
        layout->addWidget(btn);
    }

    layout->addStretch();
    return sidebar;
}

void SettingsPage::OnNavClicked(int index) {
    SetCurrentTab(index);
}

void SettingsPage::SetCurrentTab(int index) {
    if (index >= 0 && index < navButtons_.size()) {
        subStack_->setCurrentIndex(index);
        for (int i = 0; i < navButtons_.size(); ++i) {
            if (i == index) {
                navButtons_[i]->setStyleSheet(
                    "QPushButton {"
                    "  background-color: #121524;"
                    "  color: #ffffff;"
                    "  font-size: 14px;"
                    "  font-weight: bold;"
                    "  text-align: left;"
                    "  padding: 10px 20px;"
                    "  border-left: 4px solid #1976d2;"
                    "}"
                );
            } else {
                navButtons_[i]->setStyleSheet(
                    "QPushButton {"
                    "  background: transparent;"
                    "  color: #a0a4b8;"
                    "  font-size: 14px;"
                    "  text-align: left;"
                    "  padding: 10px 20px;"
                    "  border: none;"
                    "}"
                    "QPushButton:hover {"
                    "  background-color: #0d0f17;"
                    "  color: #ffffff;"
                    "}"
                );
            }
        }
    }
}

static QWidget* WrapPage(QWidget *card, const QString& title) {
    auto *wrapper = new QWidget();
    wrapper->setStyleSheet("background-color: #0b0c10;");
    auto *vbox = new QVBoxLayout(wrapper);
    vbox->setContentsMargins(40, 24, 40, 24);
    vbox->setSpacing(16);

    auto *lblTitle = new QLabel(title, wrapper);
    lblTitle->setStyleSheet("font-size: 22px; font-weight: bold; color: #ffffff;");
    vbox->addWidget(lblTitle);

    card->setStyleSheet(
        "background-color: #1a1b24;"
        "border: 1px solid #282937;"
        "border-radius: 6px;"
    );
    vbox->addWidget(card);
    vbox->addStretch();

    auto *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("border: none; background-color: #0b0c10;");
    scrollArea->setWidget(wrapper);
    return scrollArea;
}

static QWidget* CreateRowLabel(const QString& title, const QString& subtext) {
    auto *w = new QWidget();
    w->setStyleSheet("background: transparent; border: none;");
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);

    auto *lblTitle = new QLabel(title, w);
    lblTitle->setStyleSheet("color: #ffffff; font-size: 13px; font-weight: 500;");
    auto *lblSub = new QLabel(subtext, w);
    lblSub->setStyleSheet("color: #8c8f9e; font-size: 11px;");
    lblSub->setWordWrap(true);

    v->addWidget(lblTitle);
    v->addWidget(lblSub);
    return w;
}

QWidget* SettingsPage::CreateOverlayPage() {
    auto *card = new QWidget();
    auto *layout = new QGridLayout(card);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setVerticalSpacing(20);
    layout->setHorizontalSpacing(24);
    layout->setColumnStretch(0, 3);
    layout->setColumnStretch(1, 7);

    int row = 0;

    // Windowed Mode (disabled)
    layout->addWidget(CreateRowLabel("Windowed Mode", "Display widgets on a standalone window instead of an overlay tracking the target"), row, 0);
    auto *swWindowed = new ToggleSwitch("Enable", card);
    swWindowed->setChecked(false);
    swWindowed->setEnabled(false); // We use in-game Vulkan overlay
    layout->addWidget(swWindowed, row++, 1);

    // Automatic Hide
    layout->addWidget(CreateRowLabel("Automatic Hide", "Automatically disable the overlay during capture"), row, 0);
    swAutoDuringCapture_ = new ToggleSwitch("Enable", card);
    connect(swAutoDuringCapture_, &ToggleSwitch::toggled, this, [this](bool val) {
        config_->overlayHideDuringCapture = val;
        config_->Save();
        emit configChanged();
    });
    layout->addWidget(swAutoDuringCapture_, row++, 1);

    // Position
    layout->addWidget(CreateRowLabel("Position", "Where the overlay appears on the target window"), row, 0);
    quadrantPos_ = new QuadrantPositioner(card);
    connect(quadrantPos_, &QuadrantPositioner::positionChanged, this, [this](int pos) {
        config_->overlayCorner = pos;
        config_->Save();
        emit configChanged();
    });
    layout->addWidget(quadrantPos_, row++, 1);

    // Width
    layout->addWidget(CreateRowLabel("Width", "Width of the overlay window (height determined by content)"), row, 0);
    auto *wBox = new QHBoxLayout();
    sliderWidth_ = new QSlider(Qt::Horizontal, card);
    sliderWidth_->setRange(200, 1920);
    lblWidthVal_ = new QLabel("400", card);
    lblWidthVal_->setFixedWidth(40);
    lblWidthVal_->setStyleSheet("color: #64b5f6; font-weight: bold;");
    connect(sliderWidth_, &QSlider::valueChanged, this, [this](int val) {
        lblWidthVal_->setText(QString::number(val));
        config_->overlayWidth = val;
        config_->Save();
        emit configChanged();
    });
    wBox->addWidget(sliderWidth_);
    wBox->addWidget(lblWidthVal_);
    layout->addLayout(wBox, row++, 1);

    // Time Scale
    layout->addWidget(CreateRowLabel("Time Scale", "Range of time (s) displayed on graphs' x-axes. Controls the scrolling speed."), row, 0);
    auto *tsBox = new QHBoxLayout();
    sliderTimeScale_ = new QSlider(Qt::Horizontal, card);
    sliderTimeScale_->setRange(1, 100); // 0.1 to 10.0
    lblTimeScaleVal_ = new QLabel("10.0", card);
    lblTimeScaleVal_->setFixedWidth(40);
    lblTimeScaleVal_->setStyleSheet("color: #64b5f6; font-weight: bold;");
    connect(sliderTimeScale_, &QSlider::valueChanged, this, [this](int val) {
        double d = val / 10.0;
        lblTimeScaleVal_->setText(QString::number(d, 'f', 1));
        config_->overlayTimeScale = d;
        config_->Save();
        emit configChanged();
    });
    tsBox->addWidget(sliderTimeScale_);
    tsBox->addWidget(lblTimeScaleVal_);
    layout->addLayout(tsBox, row++, 1);

    // Graphics Scaling
    layout->addWidget(CreateRowLabel("Graphics Scaling", "Upscale overlay graphics to make text more readable on high DPI displays"), row, 0);
    auto *scaleBox = new QHBoxLayout();
    swScaling_ = new ToggleSwitch("Enable", card);
    sliderScalingFactor_ = new QSlider(Qt::Horizontal, card);
    sliderScalingFactor_->setRange(10, 50); // 1.0 to 5.0
    lblScalingVal_ = new QLabel("2.0", card);
    lblScalingVal_->setFixedWidth(40);
    lblScalingVal_->setStyleSheet("color: #64b5f6; font-weight: bold;");

    connect(swScaling_, &ToggleSwitch::toggled, this, [this](bool val) {
        config_->overlayGraphicsScaling = val;
        sliderScalingFactor_->setEnabled(val);
        config_->Save();
        emit configChanged();
    });
    connect(sliderScalingFactor_, &QSlider::valueChanged, this, [this](int val) {
        double d = val / 10.0;
        lblScalingVal_->setText(QString::number(d, 'f', 1));
        config_->overlayScalingFactor = d;
        config_->Save();
        emit configChanged();
    });
    scaleBox->addWidget(swScaling_);
    scaleBox->addSpacing(16);
    scaleBox->addWidget(new QLabel("Factor:", card));
    scaleBox->addWidget(sliderScalingFactor_);
    scaleBox->addWidget(lblScalingVal_);
    layout->addLayout(scaleBox, row++, 1);

    // Draw Rate
    layout->addWidget(CreateRowLabel("Draw Rate", "Rate at which to draw the overlay"), row, 0);
    auto *fpsBox = new QHBoxLayout();
    sliderDrawRate_ = new QSlider(Qt::Horizontal, card);
    sliderDrawRate_->setRange(1, 120);
    lblDrawRateVal_ = new QLabel("60", card);
    lblDrawRateVal_->setFixedWidth(40);
    lblDrawRateVal_->setStyleSheet("color: #64b5f6; font-weight: bold;");
    connect(sliderDrawRate_, &QSlider::valueChanged, this, [this](int val) {
        lblDrawRateVal_->setText(QString::number(val));
        config_->overlayDrawRate = val;
        config_->Save();
        emit configChanged();
    });
    fpsBox->addWidget(sliderDrawRate_);
    fpsBox->addWidget(lblDrawRateVal_);
    layout->addLayout(fpsBox, row++, 1);

    // Background Color
    layout->addWidget(CreateRowLabel("Background Color", "Control background color of entire overlay"), row, 0);
    btnColor_ = new ColorPickerButton("Background", config_->overlayBgColor, card);
    connect(btnColor_, &ColorPickerButton::colorChanged, this, [this](const QColor& c) {
        config_->overlayBgColor = c;
        config_->Save();
        emit configChanged();
    });
    layout->addWidget(btnColor_, row++, 1, Qt::AlignLeft);

    return WrapPage(card, "Overlay Configuration");
}

QWidget* SettingsPage::CreateDataPage() {
    auto *card = new QWidget();
    auto *layout = new QGridLayout(card);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setVerticalSpacing(20);
    layout->setHorizontalSpacing(24);
    layout->setColumnStretch(0, 3);
    layout->setColumnStretch(1, 7);

    int row = 0;

    // Polling Rate
    layout->addWidget(CreateRowLabel("Polling Rate", "Rate at which to poll API for metric data (Hz). Controls temporal resolution of graphs and readouts."), row, 0);
    auto *pBox = new QHBoxLayout();
    sliderPollRate_ = new QSlider(Qt::Horizontal, card);
    sliderPollRate_->setRange(1, 240);
    lblPollRateVal_ = new QLabel("40", card);
    lblPollRateVal_->setFixedWidth(40);
    lblPollRateVal_->setStyleSheet("color: #64b5f6; font-weight: bold;");
    connect(sliderPollRate_, &QSlider::valueChanged, this, [this](int val) {
        lblPollRateVal_->setText(QString::number(val));
        config_->dataPollingRate = val;
        config_->Save();
        emit configChanged();
    });
    pBox->addWidget(sliderPollRate_);
    pBox->addWidget(lblPollRateVal_);
    layout->addLayout(pBox, row++, 1);

    // Telemetry Period
    layout->addWidget(CreateRowLabel("Telemetry Period", "Time between service-side power telemetry polling calls (ms). Indirectly affects temporal resolution of a subset of metrics, such as GPU power and temperature."), row, 0);
    auto *tBox = new QHBoxLayout();
    sliderTelemPeriod_ = new QSlider(Qt::Horizontal, card);
    sliderTelemPeriod_->setRange(1, 500);
    lblTelemPeriodVal_ = new QLabel("100", card);
    lblTelemPeriodVal_->setFixedWidth(40);
    lblTelemPeriodVal_->setStyleSheet("color: #64b5f6; font-weight: bold;");
    connect(sliderTelemPeriod_, &QSlider::valueChanged, this, [this](int val) {
        lblTelemPeriodVal_->setText(QString::number(val));
        config_->dataTelemetryPeriod = val;
        config_->Save();
        emit configChanged();
    });
    tBox->addWidget(sliderTelemPeriod_);
    tBox->addWidget(lblTelemPeriodVal_);
    layout->addLayout(tBox, row++, 1);

    // Window Size
    layout->addWidget(CreateRowLabel("Window Size", "Size of sample window used for calculating statistics such as average or 99% (ms)"), row, 0);
    auto *wBox = new QHBoxLayout();
    sliderWindowSize_ = new QSlider(Qt::Horizontal, card);
    sliderWindowSize_->setRange(10, 5000);
    sliderWindowSize_->setSingleStep(10);
    lblWindowSizeVal_ = new QLabel("1000", card);
    lblWindowSizeVal_->setFixedWidth(40);
    lblWindowSizeVal_->setStyleSheet("color: #64b5f6; font-weight: bold;");
    connect(sliderWindowSize_, &QSlider::valueChanged, this, [this](int val) {
        lblWindowSizeVal_->setText(QString::number(val));
        config_->dataWindowSize = val;
        config_->Save();
        emit configChanged();
    });
    wBox->addWidget(sliderWindowSize_);
    wBox->addWidget(lblWindowSizeVal_);
    layout->addLayout(wBox, row++, 1);

    // Per-metric device selection
    layout->addWidget(CreateRowLabel("Per-metric device selection", "Show GPU device pickers on loadout rows. Allows tracking multiple GPUs simultaneously. When disabled, all rows track the default adapter (recommended)."), row, 0);
    swPerMetricDevice_ = new ToggleSwitch("Enable", card);
    connect(swPerMetricDevice_, &ToggleSwitch::toggled, this, [this](bool val) {
        config_->dataPerMetricDevice = val;
        config_->Save();
        emit configChanged();
    });
    layout->addWidget(swPerMetricDevice_, row++, 1);

    // Default adapter
    layout->addWidget(CreateRowLabel("Default adapter", "Global GPU for new loadout rows, frame-query adapter selection, and metrics that defer to the global adapter."), row, 0);
    comboDefaultAdapter_ = new QComboBox(card);
    comboDefaultAdapter_->addItem("AMD Radeon RX 6600 XT", "gpu0");
    comboDefaultAdapter_->addItem("Intel Arc / Iris Xe", "gpu1");
    comboDefaultAdapter_->addItem("NVIDIA GeForce GPU", "gpu2");
    layout->addWidget(comboDefaultAdapter_, row++, 1);

    return WrapPage(card, "Data Processing Configuration");
}

QWidget* SettingsPage::CreateCapturePage() {
    auto *card = new QWidget();
    auto *layout = new QGridLayout(card);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setVerticalSpacing(20);
    layout->setHorizontalSpacing(24);
    layout->setColumnStretch(0, 3);
    layout->setColumnStretch(1, 7);

    int row = 0;

    // Summary Stats
    layout->addWidget(CreateRowLabel("Summary Stats", "Generate a file that summarizes statistics over the entire capture run"), row, 0);
    swSummaryStats_ = new ToggleSwitch("Enable", card);
    connect(swSummaryStats_, &ToggleSwitch::toggled, this, [this](bool val) {
        config_->captureSummaryStats = val;
        config_->Save();
        emit configChanged();
    });
    layout->addWidget(swSummaryStats_, row++, 1);

    // Target Block List
    layout->addWidget(CreateRowLabel("Target Block List", "Filter the target process selection list to remove common non-realtime graphics applications"), row, 0);
    swTargetBlockList_ = new ToggleSwitch("Enable", card);
    connect(swTargetBlockList_, &ToggleSwitch::toggled, this, [this](bool val) {
        config_->captureTargetBlockList = val;
        config_->Save();
        emit configChanged();
    });
    layout->addWidget(swTargetBlockList_, row++, 1);

    // Capture Directory
    layout->addWidget(CreateRowLabel("Capture Directory", "Destination folder where captured CSV benchmark files are stored"), row, 0);
    auto *dirBox = new QHBoxLayout();
    txtCaptureDir_ = new QLineEdit(card);
    txtCaptureDir_->setText(config_->captureDirectory);
    auto *btnBrowse = new QPushButton("Browse...", card);
    btnBrowse->setStyleSheet("background-color: #242838; color: #fff; padding: 6px 14px; border-radius: 4px;");
    connect(btnBrowse, &QPushButton::clicked, this, &SettingsPage::OnBrowseCaptureDir);
    dirBox->addWidget(txtCaptureDir_);
    dirBox->addWidget(btnBrowse);
    layout->addLayout(dirBox, row++, 1);

    return WrapPage(card, "Capture Configuration");
}

QWidget* SettingsPage::CreateLoggingPage() {
    auto *card = new QWidget();
    auto *layout = new QGridLayout(card);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setVerticalSpacing(20);
    layout->setHorizontalSpacing(24);
    layout->setColumnStretch(0, 3);
    layout->setColumnStretch(1, 7);

    int row = 0;

    // Notice Box
    auto *noticeBox = new QWidget(card);
    noticeBox->setStyleSheet("background-color: #152338; border: 1px solid #1e3a5f; border-radius: 4px; padding: 12px;");
    auto *nbLayout = new QHBoxLayout(noticeBox);
    auto *lblNotice = new QLabel("ℹ ETL / Kernel Trace capture is currently disabled on Linux.", noticeBox);
    lblNotice->setStyleSheet("color: #64b5f6; font-size: 13px; font-weight: 500; border: none; background: transparent;");
    nbLayout->addWidget(lblNotice);
    layout->addWidget(noticeBox, row++, 0, 1, 2);

    // ETL Hotkey
    layout->addWidget(CreateRowLabel("ETL Capture Hotkey", "Hotkey for starting/finishing an ETL trace"), row, 0);
    auto *hpEtl = new HotkeyPillWidget("", card);
    layout->addWidget(hpEtl, row++, 1);

    // Capture ETL
    layout->addWidget(CreateRowLabel("Capture ETL", "Raw ETL capture is currently disabled."), row, 0);
    auto *btnEtlDisabled = new QPushButton("ETL DISABLED", card);
    btnEtlDisabled->setEnabled(false);
    btnEtlDisabled->setStyleSheet("background-color: #222530; color: #606470; border: 1px solid #323644; border-radius: 4px; padding: 8px 18px; font-weight: bold;");
    layout->addWidget(btnEtlDisabled, row++, 1, Qt::AlignLeft);

    // ETL Folder
    layout->addWidget(CreateRowLabel("ETL Folder", "Navigate to the folder that receives captured trace files"), row, 0);
    auto *btnOpenEtl = new QPushButton("OPEN IN EXPLORER", card);
    btnOpenEtl->setStyleSheet("background-color: #1976d2; color: #ffffff; border: none; border-radius: 4px; padding: 8px 20px; font-weight: bold;");
    connect(btnOpenEtl, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(config_->captureDirectory));
    });
    layout->addWidget(btnOpenEtl, row++, 1, Qt::AlignLeft);

    return WrapPage(card, "Logging Configuration");
}

QWidget* SettingsPage::CreateOtherPage() {
    auto *card = new QWidget();
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(24);

    // Section 1: Reset Preferences
    auto *grid1 = new QGridLayout();
    grid1->setContentsMargins(0, 0, 0, 0);
    grid1->setHorizontalSpacing(24);
    grid1->setColumnStretch(0, 3);
    grid1->setColumnStretch(1, 7);

    grid1->addWidget(CreateRowLabel("Reset Preferences", "Reset all preferences to their defaults"), 0, 0);
    auto *btnReset = new QPushButton("RESET", card);
    btnReset->setStyleSheet("background-color: #1976d2; color: #ffffff; border: none; border-radius: 4px; padding: 8px 24px; font-weight: bold;");
    connect(btnReset, &QPushButton::clicked, this, &SettingsPage::OnResetPreferences);
    grid1->addWidget(btnReset, 0, 1, Qt::AlignLeft);
    layout->addLayout(grid1);

    // Section 2: Linux System & Service Integration (Directly inline, no dialogs!)
    auto *div = new QWidget(card);
    div->setFixedHeight(1);
    div->setStyleSheet("background-color: #282937;");
    layout->addWidget(div);

    auto *lblSysHead = new QLabel("Linux System & Hardware Integration", card);
    lblSysHead->setStyleSheet("color: #64b5f6; font-size: 15px; font-weight: bold;");
    layout->addWidget(lblSysHead);

    auto *grid2 = new QGridLayout();
    grid2->setContentsMargins(0, 0, 0, 0);
    grid2->setVerticalSpacing(16);
    grid2->setHorizontalSpacing(24);
    grid2->setColumnStretch(0, 3);
    grid2->setColumnStretch(1, 7);

    int r = 0;

    // Daemon
    grid2->addWidget(CreateRowLabel("Background Daemon (gnumond)", "Coordinates telemetry ring buffers, GPU metrics, and headless captures"), r, 0);
    auto *dBox = new QVBoxLayout();
    lblDaemonStatus_ = new QLabel("Status: Checking...", card);
    lblDaemonStatus_->setStyleSheet("font-weight: bold; font-size: 13px; color: #ffb74d;");
    auto *dBtnBox = new QHBoxLayout();
    btnStartDaemon_ = new QPushButton("Start Daemon", card);
    btnStopDaemon_ = new QPushButton("Stop Daemon", card);
    btnEnableDaemon_ = new QPushButton("Enable at Login", card);
    btnStartDaemon_->setStyleSheet("background-color: #007799; color: white; padding: 6px 14px; border-radius: 4px; font-weight: bold;");
    btnStopDaemon_->setStyleSheet("background-color: #3e4450; color: white; padding: 6px 14px; border-radius: 4px;");
    btnEnableDaemon_->setStyleSheet("background-color: #009655; color: white; padding: 6px 14px; border-radius: 4px; font-weight: bold;");

    connect(btnStartDaemon_, &QPushButton::clicked, this, &SettingsPage::OnStartService);
    connect(btnStopDaemon_, &QPushButton::clicked, this, &SettingsPage::OnStopService);
    connect(btnEnableDaemon_, &QPushButton::clicked, this, &SettingsPage::OnEnableService);

    dBtnBox->addWidget(btnStartDaemon_);
    dBtnBox->addWidget(btnStopDaemon_);
    dBtnBox->addWidget(btnEnableDaemon_);
    dBtnBox->addStretch();

    dBox->addWidget(lblDaemonStatus_);
    dBox->addLayout(dBtnBox);
    grid2->addLayout(dBox, r++, 1);

    // Input Privileges
    grid2->addWidget(CreateRowLabel("Hardware Input Access", "Required for Click-to-Photon latency and global hotkeys under Wayland / Gamescope"), r, 0);
    auto *iBox = new QVBoxLayout();
    lblUdevStatus_ = new QLabel("udev Rules: Checking...", card);
    lblUdevStatus_->setStyleSheet("font-weight: bold; font-size: 13px;");
    lblInputAccess_ = new QLabel("Input Devices: Checking...", card);
    lblInputAccess_->setStyleSheet("font-size: 12px; color: #a0a4b8;");
    btnInstallUdev_ = new QPushButton("Grant Hardware Input Permissions (pkexec)", card);
    btnInstallUdev_->setStyleSheet("background-color: #d97706; color: white; padding: 6px 16px; border-radius: 4px; font-weight: bold;");
    connect(btnInstallUdev_, &QPushButton::clicked, this, &SettingsPage::OnInstallUdevRules);

    iBox->addWidget(lblUdevStatus_);
    iBox->addWidget(lblInputAccess_);
    iBox->addWidget(btnInstallUdev_, 0, Qt::AlignLeft);
    grid2->addLayout(iBox, r++, 1);

    // Vulkan Layers
    grid2->addWidget(CreateRowLabel("Vulkan Capture Layers", "Injects frametime metrics & in-game HUD into native and Proton / Wine games"), r, 0);
    auto *lBox = new QVBoxLayout();
    lblLayersStatus_ = new QLabel("Layer Status: Checking...", card);
    lblLayersStatus_->setStyleSheet("font-weight: bold; font-size: 13px;");
    auto *lBtnBox = new QHBoxLayout();
    btnInstallLayers_ = new QPushButton("Install / Update Layers", card);
    btnInstallLayers_->setStyleSheet("background-color: #1565c0; color: white; padding: 6px 14px; border-radius: 4px; font-weight: bold;");
    connect(btnInstallLayers_, &QPushButton::clicked, this, &SettingsPage::OnInstallLayers);
    lBtnBox->addWidget(btnInstallLayers_);
    lBtnBox->addStretch();

    lBox->addWidget(lblLayersStatus_);
    lBox->addLayout(lBtnBox);
    grid2->addLayout(lBox, r++, 1);

    layout->addLayout(grid2);

    lblSystemFeedback_ = new QLabel("", card);
    lblSystemFeedback_->setStyleSheet("color: #00e5ff; font-weight: bold; font-size: 12px;");
    layout->addWidget(lblSystemFeedback_);

    return WrapPage(card, "Other Configuration");
}

QWidget* SettingsPage::CreateAboutPage() {
    auto *card = new QWidget();
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(20);

    auto addSection = [layout, card](const QString& title, const QVector<QPair<QString, QString>>& items) {
        auto *lblSec = new QLabel(title, card);
        lblSec->setStyleSheet("color: #ffffff; font-size: 14px; font-weight: bold; margin-top: 6px;");
        layout->addWidget(lblSec);

        auto *grid = new QGridLayout();
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setVerticalSpacing(8);
        grid->setHorizontalSpacing(24);
        grid->setColumnStretch(0, 3);
        grid->setColumnStretch(1, 7);

        for (int i = 0; i < items.size(); ++i) {
            auto *lblK = new QLabel(items[i].first, card);
            lblK->setStyleSheet("color: #8c8f9e; font-size: 12px;");
            auto *lblV = new QLabel(items[i].second, card);
            lblV->setStyleSheet("color: #ffffff; font-size: 12px; font-weight: 500;");
            grid->addWidget(lblK, i, 0);
            grid->addWidget(lblV, i, 1);
        }
        layout->addLayout(grid);

        auto *line = new QWidget(card);
        line->setFixedHeight(1);
        line->setStyleSheet("background-color: #242634;");
        layout->addWidget(line);
    };

    // 1. Application
    addSection("Application", {
        {"Product", "gnumon (Intel® PresentMon Linux Port)"},
        {"Product Version", "0.1.0"},
        {"API Version", "3.4.0"},
        {"Middleware API Version", "3.4.0 Linux"},
        {"Preferences Format", "1.1.0"},
        {"Loadout Format", "1.0.0"},
        {"UI Dev Mode", "No"},
        {"Chromium Debugging", "No"},
        {"Debug Blocklist", "No"},
        {"Log Level", "Info"},
        {"Verbose Modules", "None"}
    });

    // 2. Build
    addSection("Build", {
        {"Git Hash", "b1546df0b25e22709e867b36f788b90715cf4e88"},
        {"Short Hash", "b1546df"},
        {"Build Date/Time", QString(__DATE__) + " " + QString(__TIME__)},
        {"Build Config", "Release"},
        {"Dirty Build", "No"}
    });

    // 3. Service
    addSection("Service", {
        {"Service Build ID", "gnumond-linux-x86_64-3.4.0"},
        {"Service Build Time", QString(__DATE__) + " " + QString(__TIME__)},
        {"Service Version", "2.6.0.0"}
    });

    // 4. Runtime
    struct utsname osInfo{};
    uname(&osInfo);
    addSection("Runtime", {
        {"CEF / Qt Version", QString("Qt %1").arg(QT_VERSION_STR)},
        {"Compiler", "GCC 14 (Linux x86_64)"},
        {"OS / Kernel", QString("%1 %2").arg(osInfo.sysname, osInfo.release)},
        {"Window System", QString::fromUtf8(qgetenv("XDG_SESSION_TYPE")).toUpper()}
    });

    return WrapPage(card, "About");
}

void SettingsPage::ReloadFromConfig() {
    if (!config_) return;
    swAutoDuringCapture_->setChecked(config_->overlayHideDuringCapture);
    quadrantPos_->setPosition(config_->overlayCorner);
    sliderWidth_->setValue(config_->overlayWidth);
    lblWidthVal_->setText(QString::number(config_->overlayWidth));
    sliderTimeScale_->setValue(static_cast<int>(config_->overlayTimeScale * 10));
    lblTimeScaleVal_->setText(QString::number(config_->overlayTimeScale, 'f', 1));
    swScaling_->setChecked(config_->overlayGraphicsScaling);
    sliderScalingFactor_->setValue(static_cast<int>(config_->overlayScalingFactor * 10));
    lblScalingVal_->setText(QString::number(config_->overlayScalingFactor, 'f', 1));
    sliderDrawRate_->setValue(config_->overlayDrawRate);
    lblDrawRateVal_->setText(QString::number(config_->overlayDrawRate));
    btnColor_->setColor(config_->overlayBgColor);

    sliderPollRate_->setValue(config_->dataPollingRate);
    lblPollRateVal_->setText(QString::number(config_->dataPollingRate));
    sliderTelemPeriod_->setValue(config_->dataTelemetryPeriod);
    lblTelemPeriodVal_->setText(QString::number(config_->dataTelemetryPeriod));
    sliderWindowSize_->setValue(config_->dataWindowSize);
    lblWindowSizeVal_->setText(QString::number(config_->dataWindowSize));
    swPerMetricDevice_->setChecked(config_->dataPerMetricDevice);

    swSummaryStats_->setChecked(config_->captureSummaryStats);
    swTargetBlockList_->setChecked(config_->captureTargetBlockList);
    txtCaptureDir_->setText(config_->captureDirectory);

    SetCurrentTab(0);
}

void SettingsPage::OnResetPreferences() {
    *config_ = AppConfig();
    config_->Save();
    ReloadFromConfig();
    lblSystemFeedback_->setText("Preferences have been reset to default values.");
    emit configChanged();
}

void SettingsPage::OnBrowseCaptureDir() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Capture Directory", config_->captureDirectory);
    if (!dir.isEmpty()) {
        config_->captureDirectory = dir;
        txtCaptureDir_->setText(dir);
        config_->Save();
        emit configChanged();
    }
}

QString SettingsPage::RunSetupCommand(const QString& action) {
    QString scriptPath = FindSetupScript();
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);

    if (QFileInfo::exists(scriptPath)) {
        process.start("/bin/bash", {scriptPath, action});
    } else {
        process.start(scriptPath, {action});
    }

    int timeoutMs = (action.startsWith("install")) ? 60000 : 10000;
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        return QString("Command timed out: %1").arg(action);
    }
    return QString::fromUtf8(process.readAll());
}

void SettingsPage::RefreshServiceStatus() {
    QString output = RunSetupCommand("status");
    QMap<QString, QString> statusMap;
    const auto lines = output.split('\n', Qt::SkipEmptyParts);
    for (const auto& line : lines) {
        int eq = line.indexOf('=');
        if (eq > 0) {
            statusMap.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
        }
    }

    bool userActive = (statusMap.value("user_service_active") == "active");
    bool sysActive = (statusMap.value("system_service_active") == "active");
    bool daemonRunning = userActive || sysActive;
    bool userEnabled = (statusMap.value("user_service_enabled") == "enabled");

    if (daemonRunning) {
        lblDaemonStatus_->setText(QString("Status: <span style='color:#00ff88;'>● Active / Running (%1)</span>")
                                      .arg(userActive ? "User service" : "System service"));
        btnStartDaemon_->setEnabled(false);
        btnStopDaemon_->setEnabled(true);
    } else {
        lblDaemonStatus_->setText("Status: <span style='color:#ff5555;'>○ Inactive / Stopped</span>");
        btnStartDaemon_->setEnabled(true);
        btnStopDaemon_->setEnabled(false);
    }

    if (userEnabled) {
        btnEnableDaemon_->setText("Auto-start Enabled");
        btnEnableDaemon_->setEnabled(false);
    } else {
        btnEnableDaemon_->setText("Enable Auto-start at Login");
        btnEnableDaemon_->setEnabled(true);
    }

    bool udevInstalled = (statusMap.value("udev_rules_installed") == "yes");
    bool inputAccessible = (statusMap.value("input_accessible") == "yes");

    if (udevInstalled) {
        lblUdevStatus_->setText("udev Rules: <span style='color:#00ff88;'>● Installed (/etc/udev/rules.d/99-gnumon-input.rules)</span>");
        btnInstallUdev_->setText("Re-apply Permissions (pkexec)");
    } else {
        lblUdevStatus_->setText("udev Rules: <span style='color:#ffaa00;'>▲ Not Installed</span>");
        btnInstallUdev_->setText("Grant Hardware Input Permissions (pkexec)");
    }

    if (inputAccessible) {
        lblInputAccess_->setText("Input Devices: <span style='color:#00ff88;'>● Accessible (Mouse click & global hotkeys ready)</span>");
    } else {
        lblInputAccess_->setText("Input Devices: <span style='color:#ffaa00;'>▲ Restricted (Click 'Grant Hardware Input Permissions')</span>");
    }

    bool vk64 = (statusMap.value("vulkan_64_installed") == "yes");
    bool vk32 = (statusMap.value("vulkan_32_installed") == "yes");

    lblLayersStatus_->setText(QString("Vulkan Layers: %1 | 32-bit: %2")
                                  .arg(vk64 ? "<span style='color:#00ff88;'>64-bit Ready</span>" : "<span style='color:#ff5555;'>64-bit Missing</span>")
                                  .arg(vk32 ? "<span style='color:#00ff88;'>32-bit Ready</span>" : "<span style='color:#ffaa00;'>32-bit Not Installed</span>"));
}

void SettingsPage::OnStartService() {
    lblSystemFeedback_->setText("Starting daemon service...");
    qApp->processEvents();
    QString out = RunSetupCommand("start-user");
    lblSystemFeedback_->setText(out.trimmed().isEmpty() ? "Daemon service started." : out.trimmed());
    RefreshServiceStatus();
}

void SettingsPage::OnStopService() {
    lblSystemFeedback_->setText("Stopping daemon service...");
    qApp->processEvents();
    QString out = RunSetupCommand("stop-user");
    lblSystemFeedback_->setText(out.trimmed().isEmpty() ? "Daemon service stopped." : out.trimmed());
    RefreshServiceStatus();
}

void SettingsPage::OnEnableService() {
    lblSystemFeedback_->setText("Enabling daemon service auto-start...");
    qApp->processEvents();
    QString out = RunSetupCommand("enable-user");
    lblSystemFeedback_->setText(out.trimmed().isEmpty() ? "Daemon service enabled." : out.trimmed());
    RefreshServiceStatus();
}

void SettingsPage::OnInstallUdevRules() {
    lblSystemFeedback_->setText("Requesting root permissions via pkexec to install udev rules...");
    qApp->processEvents();
    QString output = RunSetupCommand("install-udev");
    lblSystemFeedback_->setText(output.trimmed().isEmpty() ? "Udev setup finished." : output.trimmed());
    RefreshServiceStatus();
}

void SettingsPage::OnInstallLayers() {
    lblSystemFeedback_->setText("Installing / updating Vulkan layers...");
    qApp->processEvents();
    QString output = RunSetupCommand("install-layers");
    lblSystemFeedback_->setText(output.trimmed().isEmpty() ? "Layers updated." : output.trimmed());
    RefreshServiceStatus();
}

} // namespace gnumon::gui
