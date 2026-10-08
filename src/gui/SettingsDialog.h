#pragma once

#include <QDialog>
#include <QTabWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include "AppConfig.h"

namespace gnumon::gui {

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const AppConfig& currentConfig, QWidget *parent = nullptr);
    ~SettingsDialog() override = default;

    AppConfig GetConfig() const;

private slots:
    void OnPresetAll();
    void OnPresetFpsLatency();
    void OnPresetBasic();
    void OnPresetGpuFocus();
    void OnResetDefaults();
    void OnBrowseCaptureDir();
    void OnSaveDefaults();
    void OnOpacityChanged(int value);

private:
    void SetupMetricsTab(QTabWidget *tabs);
    void SetupOverlayTab(QTabWidget *tabs);
    void SetupHotkeysCaptureTab(QTabWidget *tabs);

    AppConfig config_;

    // Tab 1: Metrics
    QCheckBox *chkPresentFps_ = nullptr;
    QCheckBox *chkDisplayedFps_ = nullptr;
    QCheckBox *chkFps1PercentLow_ = nullptr;
    QCheckBox *chkFrameTime_ = nullptr;
    QCheckBox *chkLatency_ = nullptr;
    QCheckBox *chkAnimationError_ = nullptr;

    QCheckBox *chkGpuPower_ = nullptr;
    QCheckBox *chkGpuTemp_ = nullptr;
    QCheckBox *chkGpuFreq_ = nullptr;
    QCheckBox *chkGpuUtil_ = nullptr;
    QCheckBox *chkGpuVram_ = nullptr;

    QCheckBox *chkCpuPower_ = nullptr;
    QCheckBox *chkCpuTemp_ = nullptr;
    QCheckBox *chkCpuFreq_ = nullptr;
    QCheckBox *chkCpuUtil_ = nullptr;

    QCheckBox *chkShowGraph_ = nullptr;
    QSpinBox *spinAveragingWindow_ = nullptr;

    // Tab 2: Overlay
    QComboBox *comboOverlayCorner_ = nullptr;
    QSlider *sliderOverlayOpacity_ = nullptr;
    QLabel *lblOpacityVal_ = nullptr;
    QComboBox *comboOverlayScale_ = nullptr;
    QCheckBox *chkOverlayShowGraph_ = nullptr;

    QCheckBox *chkInGameHudEnabled_ = nullptr;
    QComboBox *comboInGameHudCorner_ = nullptr;

    // Tab 3: Hotkeys & Capture
    QLineEdit *editHotkeyOverlay_ = nullptr;
    QLineEdit *editHotkeyRecording_ = nullptr;
    QLineEdit *editHotkeyInGameHud_ = nullptr;
    QLineEdit *editHotkeyMiniHud_ = nullptr;

    QLineEdit *editCaptureDir_ = nullptr;
    QPushButton *btnBrowseCaptureDir_ = nullptr;
    QCheckBox *chkAutoOpenCaptures_ = nullptr;
};

} // namespace gnumon::gui
