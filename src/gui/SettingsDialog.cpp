#include "SettingsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMessageBox>

namespace gnumon::gui {

SettingsDialog::SettingsDialog(const AppConfig& currentConfig, QWidget *parent)
    : QDialog(parent), config_(currentConfig)
{
    setWindowTitle("PresentMon Preferences & Settings — gnumon");
    setModal(true);
    resize(580, 520);
    setStyleSheet(
        "QDialog { background-color: #1e222b; color: #eceff1; }"
        "QGroupBox { font-weight: bold; border: 1px solid #37474f; border-radius: 6px; margin-top: 10px; padding-top: 10px; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 10px; padding: 0 4px; color: #00e5ff; }"
        "QTabWidget::pane { border: 1px solid #37474f; background-color: #232834; border-radius: 4px; }"
        "QTabBar::tab { background: #1e222b; color: #b0bec5; padding: 8px 18px; border-top-left-radius: 4px; border-top-right-radius: 4px; }"
        "QTabBar::tab:selected { background: #232834; color: #00e5ff; font-weight: bold; border-bottom: 2px solid #00e5ff; }"
        "QPushButton { background-color: #263238; color: #eceff1; border: 1px solid #455a64; border-radius: 4px; padding: 6px 14px; }"
        "QPushButton:hover { background-color: #37474f; border-color: #00e5ff; }"
        "QPushButton:pressed { background-color: #00acc1; color: #101216; }"
        "QLineEdit, QComboBox, QSpinBox { background-color: #181b22; color: #eceff1; border: 1px solid #37474f; border-radius: 4px; padding: 4px 8px; }"
        "QCheckBox { color: #cfd8dc; spacing: 6px; }"
        "QCheckBox::indicator:checked { background-color: #00e5ff; border: 1px solid #00e5ff; border-radius: 2px; }"
    );

    auto *mainLayout = new QVBoxLayout(this);

    auto *tabs = new QTabWidget(this);
    SetupMetricsTab(tabs);
    SetupOverlayTab(tabs);
    SetupHotkeysCaptureTab(tabs);
    mainLayout->addWidget(tabs);

    // Bottom Action Buttons
    auto *bottomLayout = new QHBoxLayout();

    auto *btnSaveDefault = new QPushButton("Save as Default (config.ini)", this);
    btnSaveDefault->setStyleSheet("background-color: #00838f; color: white; font-weight: bold;");
    connect(btnSaveDefault, &QPushButton::clicked, this, &SettingsDialog::OnSaveDefaults);

    auto *btnReset = new QPushButton("Reset Defaults", this);
    connect(btnReset, &QPushButton::clicked, this, &SettingsDialog::OnResetDefaults);

    auto *btnApply = new QPushButton("Apply", this);
    connect(btnApply, &QPushButton::clicked, this, &QDialog::accept);

    auto *btnCancel = new QPushButton("Cancel", this);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);

    bottomLayout->addWidget(btnSaveDefault);
    bottomLayout->addWidget(btnReset);
    bottomLayout->addStretch();
    bottomLayout->addWidget(btnApply);
    bottomLayout->addWidget(btnCancel);

    mainLayout->addLayout(bottomLayout);
}

void SettingsDialog::SetupMetricsTab(QTabWidget *tabs) {
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);

    // Presets Row
    auto *presetGroup = new QGroupBox("Quick Telemetry Presets", tab);
    auto *presetLayout = new QHBoxLayout(presetGroup);

    auto *btnAll = new QPushButton("All Telemetry", tab);
    auto *btnFpsLat = new QPushButton("FPS & Latency", tab);
    auto *btnBasic = new QPushButton("Minimal", tab);
    auto *btnGpu = new QPushButton("Graphics Focus", tab);

    connect(btnAll, &QPushButton::clicked, this, &SettingsDialog::OnPresetAll);
    connect(btnFpsLat, &QPushButton::clicked, this, &SettingsDialog::OnPresetFpsLatency);
    connect(btnBasic, &QPushButton::clicked, this, &SettingsDialog::OnPresetBasic);
    connect(btnGpu, &QPushButton::clicked, this, &SettingsDialog::OnPresetGpuFocus);

    presetLayout->addWidget(btnAll);
    presetLayout->addWidget(btnFpsLat);
    presetLayout->addWidget(btnBasic);
    presetLayout->addWidget(btnGpu);
    layout->addWidget(presetGroup);

    // Group: Framerate & Display Pacing
    auto *fpsGroup = new QGroupBox("Framerate & Display Pacing", tab);
    auto *fpsLayout = new QGridLayout(fpsGroup);

    chkPresentFps_ = new QCheckBox("Present FPS (App Target)", tab);
    chkPresentFps_->setChecked(config_.showPresentFps);
    chkDisplayedFps_ = new QCheckBox("Display FPS (Screen Output)", tab);
    chkDisplayedFps_->setChecked(config_.showDisplayedFps);
    chkFps1PercentLow_ = new QCheckBox("1% Low FPS (Stability)", tab);
    chkFps1PercentLow_->setChecked(config_.showFps1PercentLow);
    chkFrameTime_ = new QCheckBox("Frame Time (ms)", tab);
    chkFrameTime_->setChecked(config_.showFrameTime);

    fpsLayout->addWidget(chkPresentFps_, 0, 0);
    fpsLayout->addWidget(chkDisplayedFps_, 0, 1);
    fpsLayout->addWidget(chkFps1PercentLow_, 1, 0);
    fpsLayout->addWidget(chkFrameTime_, 1, 1);
    layout->addWidget(fpsGroup);

    // Group: Latency & Stutter
    auto *latGroup = new QGroupBox("Latency & Temporal Pacing", tab);
    auto *latLayout = new QGridLayout(latGroup);

    chkLatency_ = new QCheckBox("Render-to-Display Latency (ms)", tab);
    chkLatency_->setChecked(config_.showLatency);
    chkAnimationError_ = new QCheckBox("Animation Error (ms temporal wobble)", tab);
    chkAnimationError_->setChecked(config_.showAnimationError);

    latLayout->addWidget(chkLatency_, 0, 0);
    latLayout->addWidget(chkAnimationError_, 0, 1);
    layout->addWidget(latGroup);

    // Group: Hardware Telemetry (GPU & CPU)
    auto *hwGroup = new QGroupBox("Hardware Telemetry (GPU / CPU)", tab);
    auto *hwLayout = new QGridLayout(hwGroup);

    chkGpuPower_ = new QCheckBox("GPU Power (W)", tab);
    chkGpuPower_->setChecked(config_.showGpuPower);
    chkGpuTemp_ = new QCheckBox("GPU Temperature (°C)", tab);
    chkGpuTemp_->setChecked(config_.showGpuTemp);
    chkGpuFreq_ = new QCheckBox("GPU Clock (MHz)", tab);
    chkGpuFreq_->setChecked(config_.showGpuFreq);
    chkGpuUtil_ = new QCheckBox("GPU Utilization (%)", tab);
    chkGpuUtil_->setChecked(config_.showGpuUtil);
    chkGpuVram_ = new QCheckBox("VRAM Allocation (MB)", tab);
    chkGpuVram_->setChecked(config_.showGpuVram);

    chkCpuPower_ = new QCheckBox("CPU Package Power (W)", tab);
    chkCpuPower_->setChecked(config_.showCpuPower);
    chkCpuTemp_ = new QCheckBox("CPU Temperature (°C)", tab);
    chkCpuTemp_->setChecked(config_.showCpuTemp);
    chkCpuFreq_ = new QCheckBox("CPU Clock (MHz)", tab);
    chkCpuFreq_->setChecked(config_.showCpuFreq);
    chkCpuUtil_ = new QCheckBox("CPU Utilization (%)", tab);
    chkCpuUtil_->setChecked(config_.showCpuUtil);

    hwLayout->addWidget(chkGpuPower_, 0, 0);
    hwLayout->addWidget(chkGpuTemp_, 0, 1);
    hwLayout->addWidget(chkGpuFreq_, 0, 2);
    hwLayout->addWidget(chkGpuUtil_, 1, 0);
    hwLayout->addWidget(chkGpuVram_, 1, 1);

    hwLayout->addWidget(chkCpuPower_, 2, 0);
    hwLayout->addWidget(chkCpuTemp_, 2, 1);
    hwLayout->addWidget(chkCpuFreq_, 2, 2);
    hwLayout->addWidget(chkCpuUtil_, 3, 0);
    layout->addWidget(hwGroup);

    // Averaging Window & Graph
    auto *miscGroup = new QGroupBox("Averaging & Graph", tab);
    auto *miscLayout = new QHBoxLayout(miscGroup);

    chkShowGraph_ = new QCheckBox("Show Real-time Graph", tab);
    chkShowGraph_->setChecked(config_.showGraph);

    auto *lblAvg = new QLabel("Rolling Average Window:", tab);
    spinAveragingWindow_ = new QSpinBox(tab);
    spinAveragingWindow_->setRange(250, 5000);
    spinAveragingWindow_->setSingleStep(250);
    spinAveragingWindow_->setSuffix(" ms");
    spinAveragingWindow_->setValue(config_.averagingWindowMs);

    miscLayout->addWidget(chkShowGraph_);
    miscLayout->addStretch();
    miscLayout->addWidget(lblAvg);
    miscLayout->addWidget(spinAveragingWindow_);
    layout->addWidget(miscGroup);

    tabs->addTab(tab, "Metrics");
}

void SettingsDialog::SetupOverlayTab(QTabWidget *tabs) {
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);

    // Group: Desktop Overlay
    auto *deskGroup = new QGroupBox("Desktop Transparent Overlay (F11)", tab);
    auto *deskLayout = new QGridLayout(deskGroup);

    auto *lblCorner = new QLabel("Screen Corner Position:", tab);
    comboOverlayCorner_ = new QComboBox(tab);
    comboOverlayCorner_->addItem("Top-Left (Default)", 0);
    comboOverlayCorner_->addItem("Top-Right", 1);
    comboOverlayCorner_->addItem("Bottom-Left", 2);
    comboOverlayCorner_->addItem("Bottom-Right", 3);
    comboOverlayCorner_->setCurrentIndex(config_.overlayCorner);

    auto *lblOpacity = new QLabel("Background Opacity:", tab);
    sliderOverlayOpacity_ = new QSlider(Qt::Horizontal, tab);
    sliderOverlayOpacity_->setRange(20, 100);
    sliderOverlayOpacity_->setValue(config_.overlayOpacity);
    lblOpacityVal_ = new QLabel(QString("%1%").arg(config_.overlayOpacity), tab);
    lblOpacityVal_->setMinimumWidth(40);
    connect(sliderOverlayOpacity_, &QSlider::valueChanged, this, &SettingsDialog::OnOpacityChanged);

    auto *lblScale = new QLabel("Scale / Font Size:", tab);
    comboOverlayScale_ = new QComboBox(tab);
    comboOverlayScale_->addItem("Compact (75%)", 75);
    comboOverlayScale_->addItem("Standard (100%)", 100);
    comboOverlayScale_->addItem("Large (125%)", 125);
    comboOverlayScale_->addItem("Extra Large (150%)", 150);
    int scaleIdx = comboOverlayScale_->findData(config_.overlayScale);
    if (scaleIdx >= 0) comboOverlayScale_->setCurrentIndex(scaleIdx);

    chkOverlayShowGraph_ = new QCheckBox("Display Frametime Graph inside Overlay", tab);
    chkOverlayShowGraph_->setChecked(config_.overlayShowGraph);

    deskLayout->addWidget(lblCorner, 0, 0);
    deskLayout->addWidget(comboOverlayCorner_, 0, 1, 1, 2);
    deskLayout->addWidget(lblOpacity, 1, 0);
    deskLayout->addWidget(sliderOverlayOpacity_, 1, 1);
    deskLayout->addWidget(lblOpacityVal_, 1, 2);
    deskLayout->addWidget(lblScale, 2, 0);
    deskLayout->addWidget(comboOverlayScale_, 2, 1, 1, 2);
    deskLayout->addWidget(chkOverlayShowGraph_, 3, 0, 1, 3);
    layout->addWidget(deskGroup);

    // Group: In-Game Vulkan Swapchain HUD
    auto *hudGroup = new QGroupBox("In-Game Vulkan Swapchain HUD (Direct in Swapchain)", tab);
    auto *hudLayout = new QGridLayout(hudGroup);

    chkInGameHudEnabled_ = new QCheckBox("Enable In-Game Swapchain HUD by default on game launch", tab);
    chkInGameHudEnabled_->setChecked(config_.inGameHudEnabled);

    auto *lblHudCorner = new QLabel("In-Game HUD Position:", tab);
    comboInGameHudCorner_ = new QComboBox(tab);
    comboInGameHudCorner_->addItem("Top-Left", 0);
    comboInGameHudCorner_->addItem("Top-Right", 1);
    comboInGameHudCorner_->addItem("Bottom-Left", 2);
    comboInGameHudCorner_->addItem("Bottom-Right", 3);
    comboInGameHudCorner_->setCurrentIndex(config_.inGameHudCorner);

    hudLayout->addWidget(chkInGameHudEnabled_, 0, 0, 1, 2);
    hudLayout->addWidget(lblHudCorner, 1, 0);
    hudLayout->addWidget(comboInGameHudCorner_, 1, 1);
    layout->addWidget(hudGroup);

    layout->addStretch();
    tabs->addTab(tab, "Overlay & HUD");
}

void SettingsDialog::SetupHotkeysCaptureTab(QTabWidget *tabs) {
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);

    // Group: Hotkeys
    auto *hotGroup = new QGroupBox("Keyboard Shortcuts / Hotkeys", tab);
    auto *hotLayout = new QGridLayout(hotGroup);

    auto *lblHotOverlay = new QLabel("Toggle Desktop Overlay:", tab);
    editHotkeyOverlay_ = new QLineEdit(config_.hotkeyOverlay, tab);
    editHotkeyOverlay_->setPlaceholderText("F11");

    auto *lblHotInGame = new QLabel("Toggle In-Game HUD:", tab);
    editHotkeyInGameHud_ = new QLineEdit(config_.hotkeyInGameHud, tab);
    editHotkeyInGameHud_->setPlaceholderText("F9");

    auto *lblHotRec = new QLabel("Toggle Capture (CSV):", tab);
    editHotkeyRecording_ = new QLineEdit(config_.hotkeyRecording, tab);
    editHotkeyRecording_->setPlaceholderText("F10");

    auto *lblHotMini = new QLabel("Toggle Mini HUD Window:", tab);
    editHotkeyMiniHud_ = new QLineEdit(config_.hotkeyMiniHud, tab);
    editHotkeyMiniHud_->setPlaceholderText("F12");

    hotLayout->addWidget(lblHotOverlay, 0, 0);
    hotLayout->addWidget(editHotkeyOverlay_, 0, 1);
    hotLayout->addWidget(lblHotInGame, 1, 0);
    hotLayout->addWidget(editHotkeyInGameHud_, 1, 1);
    hotLayout->addWidget(lblHotRec, 2, 0);
    hotLayout->addWidget(editHotkeyRecording_, 2, 1);
    hotLayout->addWidget(lblHotMini, 3, 0);
    hotLayout->addWidget(editHotkeyMiniHud_, 3, 1);
    layout->addWidget(hotGroup);

    // Group: Captures
    auto *capGroup = new QGroupBox("Capture & CSV Export Settings", tab);
    auto *capLayout = new QGridLayout(capGroup);

    auto *lblCapDir = new QLabel("Capture Output Folder:", tab);
    editCaptureDir_ = new QLineEdit(config_.captureDirectory, tab);
    btnBrowseCaptureDir_ = new QPushButton("Browse...", tab);
    connect(btnBrowseCaptureDir_, &QPushButton::clicked, this, &SettingsDialog::OnBrowseCaptureDir);

    chkAutoOpenCaptures_ = new QCheckBox("Automatically open captures directory after stopping capture", tab);
    chkAutoOpenCaptures_->setChecked(config_.autoOpenCaptures);

    capLayout->addWidget(lblCapDir, 0, 0);
    capLayout->addWidget(editCaptureDir_, 0, 1);
    capLayout->addWidget(btnBrowseCaptureDir_, 0, 2);
    capLayout->addWidget(chkAutoOpenCaptures_, 1, 0, 1, 3);
    layout->addWidget(capGroup);

    layout->addStretch();
    tabs->addTab(tab, "Hotkeys & Capture");
}

void SettingsDialog::OnOpacityChanged(int value) {
    if (lblOpacityVal_) {
        lblOpacityVal_->setText(QString("%1%").arg(value));
    }
}

void SettingsDialog::OnBrowseCaptureDir() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Captures Directory", editCaptureDir_->text());
    if (!dir.isEmpty()) {
        editCaptureDir_->setText(dir);
    }
}

void SettingsDialog::OnPresetAll() {
    chkPresentFps_->setChecked(true);
    chkDisplayedFps_->setChecked(true);
    chkFps1PercentLow_->setChecked(true);
    chkFrameTime_->setChecked(true);
    chkLatency_->setChecked(true);
    chkAnimationError_->setChecked(true);
    chkGpuPower_->setChecked(true);
    chkGpuTemp_->setChecked(true);
    chkGpuFreq_->setChecked(true);
    chkGpuUtil_->setChecked(true);
    chkGpuVram_->setChecked(true);
    chkCpuPower_->setChecked(true);
    chkCpuTemp_->setChecked(true);
    chkCpuFreq_->setChecked(true);
    chkCpuUtil_->setChecked(true);
}

void SettingsDialog::OnPresetFpsLatency() {
    chkPresentFps_->setChecked(true);
    chkDisplayedFps_->setChecked(true);
    chkFps1PercentLow_->setChecked(true);
    chkFrameTime_->setChecked(true);
    chkLatency_->setChecked(true);
    chkAnimationError_->setChecked(true);
    chkGpuPower_->setChecked(false);
    chkGpuTemp_->setChecked(false);
    chkGpuFreq_->setChecked(false);
    chkGpuUtil_->setChecked(true);
    chkGpuVram_->setChecked(false);
    chkCpuPower_->setChecked(false);
    chkCpuTemp_->setChecked(false);
    chkCpuFreq_->setChecked(false);
    chkCpuUtil_->setChecked(true);
}

void SettingsDialog::OnPresetBasic() {
    chkPresentFps_->setChecked(true);
    chkDisplayedFps_->setChecked(false);
    chkFps1PercentLow_->setChecked(true);
    chkFrameTime_->setChecked(true);
    chkLatency_->setChecked(false);
    chkAnimationError_->setChecked(false);
    chkGpuPower_->setChecked(false);
    chkGpuTemp_->setChecked(false);
    chkGpuFreq_->setChecked(false);
    chkGpuUtil_->setChecked(false);
    chkGpuVram_->setChecked(false);
    chkCpuPower_->setChecked(false);
    chkCpuTemp_->setChecked(false);
    chkCpuFreq_->setChecked(false);
    chkCpuUtil_->setChecked(false);
}

void SettingsDialog::OnPresetGpuFocus() {
    chkPresentFps_->setChecked(true);
    chkDisplayedFps_->setChecked(true);
    chkFps1PercentLow_->setChecked(true);
    chkFrameTime_->setChecked(true);
    chkLatency_->setChecked(false);
    chkAnimationError_->setChecked(false);
    chkGpuPower_->setChecked(true);
    chkGpuTemp_->setChecked(true);
    chkGpuFreq_->setChecked(true);
    chkGpuUtil_->setChecked(true);
    chkGpuVram_->setChecked(true);
    chkCpuPower_->setChecked(false);
    chkCpuTemp_->setChecked(false);
    chkCpuFreq_->setChecked(false);
    chkCpuUtil_->setChecked(false);
}

void SettingsDialog::OnResetDefaults() {
    AppConfig def;
    config_ = def;

    chkPresentFps_->setChecked(def.showPresentFps);
    chkDisplayedFps_->setChecked(def.showDisplayedFps);
    chkFps1PercentLow_->setChecked(def.showFps1PercentLow);
    chkFrameTime_->setChecked(def.showFrameTime);
    chkLatency_->setChecked(def.showLatency);
    chkAnimationError_->setChecked(def.showAnimationError);
    chkGpuPower_->setChecked(def.showGpuPower);
    chkGpuTemp_->setChecked(def.showGpuTemp);
    chkGpuFreq_->setChecked(def.showGpuFreq);
    chkGpuUtil_->setChecked(def.showGpuUtil);
    chkGpuVram_->setChecked(def.showGpuVram);
    chkCpuPower_->setChecked(def.showCpuPower);
    chkCpuTemp_->setChecked(def.showCpuTemp);
    chkCpuFreq_->setChecked(def.showCpuFreq);
    chkCpuUtil_->setChecked(def.showCpuUtil);
    chkShowGraph_->setChecked(def.showGraph);
    spinAveragingWindow_->setValue(def.averagingWindowMs);

    comboOverlayCorner_->setCurrentIndex(def.overlayCorner);
    sliderOverlayOpacity_->setValue(def.overlayOpacity);
    comboOverlayScale_->setCurrentIndex(comboOverlayScale_->findData(def.overlayScale));
    chkOverlayShowGraph_->setChecked(def.overlayShowGraph);

    chkInGameHudEnabled_->setChecked(def.inGameHudEnabled);
    comboInGameHudCorner_->setCurrentIndex(def.inGameHudCorner);

    editHotkeyOverlay_->setText(def.hotkeyOverlay);
    editHotkeyRecording_->setText(def.hotkeyRecording);
    editHotkeyInGameHud_->setText(def.hotkeyInGameHud);
    editHotkeyMiniHud_->setText(def.hotkeyMiniHud);

    editCaptureDir_->setText(def.captureDirectory);
    chkAutoOpenCaptures_->setChecked(def.autoOpenCaptures);
}

AppConfig SettingsDialog::GetConfig() const {
    AppConfig cfg;

    cfg.showPresentFps = chkPresentFps_->isChecked();
    cfg.showDisplayedFps = chkDisplayedFps_->isChecked();
    cfg.showFps1PercentLow = chkFps1PercentLow_->isChecked();
    cfg.showFrameTime = chkFrameTime_->isChecked();
    cfg.showLatency = chkLatency_->isChecked();
    cfg.showAnimationError = chkAnimationError_->isChecked();

    cfg.showGpuPower = chkGpuPower_->isChecked();
    cfg.showGpuTemp = chkGpuTemp_->isChecked();
    cfg.showGpuFreq = chkGpuFreq_->isChecked();
    cfg.showGpuUtil = chkGpuUtil_->isChecked();
    cfg.showGpuVram = chkGpuVram_->isChecked();

    cfg.showCpuPower = chkCpuPower_->isChecked();
    cfg.showCpuTemp = chkCpuTemp_->isChecked();
    cfg.showCpuFreq = chkCpuFreq_->isChecked();
    cfg.showCpuUtil = chkCpuUtil_->isChecked();

    cfg.showGraph = chkShowGraph_->isChecked();
    cfg.averagingWindowMs = spinAveragingWindow_->value();

    cfg.overlayCorner = comboOverlayCorner_->currentIndex();
    cfg.overlayOpacity = sliderOverlayOpacity_->value();
    cfg.overlayScale = comboOverlayScale_->currentData().toInt();
    cfg.overlayShowGraph = chkOverlayShowGraph_->isChecked();

    cfg.inGameHudEnabled = chkInGameHudEnabled_->isChecked();
    cfg.inGameHudCorner = comboInGameHudCorner_->currentIndex();

    cfg.hotkeyOverlay = editHotkeyOverlay_->text().trimmed();
    cfg.hotkeyRecording = editHotkeyRecording_->text().trimmed();
    cfg.hotkeyInGameHud = editHotkeyInGameHud_->text().trimmed();
    cfg.hotkeyMiniHud = editHotkeyMiniHud_->text().trimmed();

    cfg.captureDirectory = editCaptureDir_->text().trimmed();
    cfg.autoOpenCaptures = chkAutoOpenCaptures_->isChecked();

    return cfg;
}

void SettingsDialog::OnSaveDefaults() {
    AppConfig cfg = GetConfig();
    cfg.Save();
    accept();
}

} // namespace gnumon::gui
