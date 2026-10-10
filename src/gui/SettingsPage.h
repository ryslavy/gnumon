#pragma once

#include <QWidget>
#include <QStackedWidget>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QLineEdit>
#include <QComboBox>
#include <QTimer>
#include "AppConfig.h"
#include "ToggleSwitch.h"
#include "HotkeyPillWidget.h"
#include "QuadrantPositioner.h"
#include "ColorPickerButton.h"

namespace gnumon::gui {

class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(AppConfig *config, QWidget *parent = nullptr);

    void ReloadFromConfig();
    void SetCurrentTab(int index);

signals:
    void topRequested();
    void configChanged();

private slots:
    void OnNavClicked(int index);
    void OnResetPreferences();
    void OnBrowseCaptureDir();

    // Linux Service setup slots
    void RefreshServiceStatus();
    void OnStartService();
    void OnStopService();
    void OnEnableService();
    void OnInstallUdevRules();
    void OnInstallLayers();

private:
    void SetupUi();
    QWidget* CreateSidebar();
    QWidget* CreateOverlayPage();
    QWidget* CreateDataPage();
    QWidget* CreateCapturePage();
    QWidget* CreateLoggingPage();
    QWidget* CreateOtherPage();
    QWidget* CreateAboutPage();

    QString RunSetupCommand(const QString& action);

    AppConfig *config_ = nullptr;
    QStackedWidget *subStack_ = nullptr;
    QVector<QPushButton*> navButtons_;

    // Overlay controls
    ToggleSwitch *swAutoDuringCapture_ = nullptr;
    QuadrantPositioner *quadrantPos_ = nullptr;
    QSlider *sliderWidth_ = nullptr;
    QLabel *lblWidthVal_ = nullptr;
    QSlider *sliderTimeScale_ = nullptr;
    QLabel *lblTimeScaleVal_ = nullptr;
    ToggleSwitch *swScaling_ = nullptr;
    QSlider *sliderScalingFactor_ = nullptr;
    QLabel *lblScalingVal_ = nullptr;
    QSlider *sliderDrawRate_ = nullptr;
    QLabel *lblDrawRateVal_ = nullptr;
    ColorPickerButton *btnColor_ = nullptr;

    // Data controls
    QSlider *sliderPollRate_ = nullptr;
    QLabel *lblPollRateVal_ = nullptr;
    QSlider *sliderTelemPeriod_ = nullptr;
    QLabel *lblTelemPeriodVal_ = nullptr;
    QSlider *sliderWindowSize_ = nullptr;
    QLabel *lblWindowSizeVal_ = nullptr;
    ToggleSwitch *swPerMetricDevice_ = nullptr;
    QComboBox *comboDefaultAdapter_ = nullptr;

    // Capture controls
    ToggleSwitch *swSummaryStats_ = nullptr;
    ToggleSwitch *swTargetBlockList_ = nullptr;
    QLineEdit *txtCaptureDir_ = nullptr;

    // Other / System integration controls
    QLabel *lblDaemonStatus_ = nullptr;
    QPushButton *btnStartDaemon_ = nullptr;
    QPushButton *btnStopDaemon_ = nullptr;
    QPushButton *btnEnableDaemon_ = nullptr;
    QLabel *lblUdevStatus_ = nullptr;
    QLabel *lblInputAccess_ = nullptr;
    QPushButton *btnInstallUdev_ = nullptr;
    QLabel *lblLayersStatus_ = nullptr;
    QPushButton *btnInstallLayers_ = nullptr;
    bool layersInstalled_ = false;
    QLabel *lblSystemFeedback_ = nullptr;
    QTimer *serviceTimer_ = nullptr;
};

} // namespace gnumon::gui
