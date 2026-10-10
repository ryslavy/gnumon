#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QLabel>
#include <QTimer>
#include <QKeyEvent>
#include <fstream>
#include <vector>
#include <string>

#include "../../include/gnumon/PresentMonAPI.h"
#include "AppConfig.h"
#include "MainViewWidget.h"
#include "SettingsPage.h"
#include "LoadoutConfigPage.h"
#include "WindowedOverlayWidget.h"

namespace gnumon::gui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void OnOpenFullMetrics();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void OnPollTimer();
    void OnProcessChanged(uint32_t pid, const QString& name);
    void OnToggleOverlay();
    void OnCyclePreset();
    void OnToggleRecording();
    void OnOpenCapturesFolder();
    void OnEditLoadout();
    void OnOpenSettings();
    void OnBackToMain();
    void OnConfigChanged();

private:
    void SetupUi();
    void RefreshProcesses();
    void AutoTargetProcess();
    void UpdateStatusBar();
    QString GetCapturesDirectory() const;
    void WriteCaptureSummary();

    struct CaptureStats {
        std::vector<double> frameTimesMs;
        std::vector<double> fpsList;
        double sumGpuUtil = 0.0;
        double sumGpuTemp = 0.0;
        double sumGpuPower = 0.0;
        double sumGpuFreq = 0.0;
        double sumCpuUtil = 0.0;
        double sumCpuTemp = 0.0;
        double sumCpuPower = 0.0;
        double sumCpuFreq = 0.0;
        uint64_t sampleCount = 0;

        void Reset() {
            frameTimesMs.clear();
            fpsList.clear();
            sumGpuUtil = 0.0;
            sumGpuTemp = 0.0;
            sumGpuPower = 0.0;
            sumGpuFreq = 0.0;
            sumCpuUtil = 0.0;
            sumCpuTemp = 0.0;
            sumCpuPower = 0.0;
            sumCpuFreq = 0.0;
            sampleCount = 0;
        }

        void AddFrame(double ftMs, double gUtil, double gTemp, double gPwr, double gFreq,
                      double cUtil, double cTemp, double cPwr) {
            if (ftMs > 0.0001) {
                frameTimesMs.push_back(ftMs);
                fpsList.push_back(1000.0 / ftMs);
            }
            sumGpuUtil += gUtil;
            sumGpuTemp += gTemp;
            sumGpuPower += gPwr;
            sumGpuFreq += gFreq;
            sumCpuUtil += cUtil;
            sumCpuTemp += cTemp;
            sumCpuPower += cPwr;
            sampleCount++;
        }
    };
    CaptureStats captureStats_{};

    PM_SESSION_HANDLE session_ = nullptr;
    PM_FRAME_QUERY_HANDLE frameQuery_ = nullptr;
    uint32_t frameBlobSize_ = 0;

    QTimer *pollTimer_ = nullptr;
    QTimer *procRefreshTimer_ = nullptr;

    AppConfig config_{};
    uint32_t trackedPid_ = 0;
    QString trackedProcessName_ = "";
    bool isRecording_ = false;
    uint64_t recordedFramesCount_ = 0;
    uint64_t recordingStartMs_ = 0;
    std::ofstream csvFile_;
    QString currentCapturePath_;
    bool inGameOverlayActive_ = true;

    // UI Stack
    QStackedWidget *rootStack_ = nullptr;
    MainViewWidget *mainView_ = nullptr;
    SettingsPage *settingsPage_ = nullptr;
    LoadoutConfigPage *loadoutPage_ = nullptr;
    WindowedOverlayWidget *windowedOverlay_ = nullptr;

    // Bottom Status Bar
    QWidget *statusBarWidget_ = nullptr;
    QLabel *lblStatusProcess_ = nullptr;
    QLabel *lblStatusRec_ = nullptr;
    QLabel *lblStatusHide_ = nullptr;
    QLabel *lblStatusPoll_ = nullptr;
    QLabel *lblStatusFps_ = nullptr;
};

} // namespace gnumon::gui
