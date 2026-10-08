#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QTimer>
#include <QProgressBar>
#include <fstream>
#include "../../include/gnumon/PresentMonAPI.h"
#include "FrametimeGraphWidget.h"
#include "MetricsConfigDialog.h"
#include "PresentMonOverlay.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPoint>

namespace gnumon::gui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private slots:
    void OnPollTimer();
    void OnToggleRecording();
    void OnToggleOverlay();
    void OnOpenCapturesFolder();
    void OnProcessChanged(int index);
    void OnRefreshProcesses();
    void OnConfigureMetrics();
    void OnToggleLayerInstall();
    void OnToggleMiniOverlay();

private:
    void SetupUi();
    void PopulateProcessList();
    void ApplyMetricsConfig();
    QString GetCapturesDirectory() const;

    PM_SESSION_HANDLE session_ = nullptr;
    PM_DYNAMIC_QUERY_HANDLE query_ = nullptr;
    PM_FRAME_QUERY_HANDLE frameQuery_ = nullptr;
    uint32_t frameBlobSize_ = 0;
    QTimer *pollTimer_ = nullptr;
    PresentMonOverlay *overlay_ = nullptr;

    // UI elements
    QComboBox *comboProcess_ = nullptr;
    QPushButton *btnRefreshProcess_ = nullptr;
    QPushButton *btnConfigMetrics_ = nullptr;
    QPushButton *btnInstallLayer_ = nullptr;
    QPushButton *btnOverlay_ = nullptr;
    QPushButton *btnMiniOverlay_ = nullptr;
    QPushButton *btnOpenCaptures_ = nullptr;
    QPushButton *btnRecord_ = nullptr;
    QString currentCapturePath_;
    QWidget *topContainer_ = nullptr;
    FrametimeGraphWidget *graphWidget_ = nullptr;
    QWidget *graphGroup_ = nullptr;

    QWidget *gpuGroup_ = nullptr;
    QLabel *lblGpuName_ = nullptr;
    QLabel *lblGpuPower_ = nullptr;
    QLabel *lblGpuTemp_ = nullptr;
    QLabel *lblGpuUtil_ = nullptr;
    QLabel *lblGpuFreq_ = nullptr;
    QLabel *lblGpuVram_ = nullptr;
    QProgressBar *barGpuUtil_ = nullptr;

    QWidget *cpuGroup_ = nullptr;
    QLabel *lblCpuName_ = nullptr;
    QLabel *lblCpuUtil_ = nullptr;
    QLabel *lblCpuPower_ = nullptr;
    QLabel *lblCpuTemp_ = nullptr;
    QLabel *lblCpuFreq_ = nullptr;
    QProgressBar *barCpuUtil_ = nullptr;

    QLabel *lblStatus_ = nullptr;
    bool isRecording_ = false;
    bool isMiniOverlay_ = false;
    QPoint dragPosition_;
    uint32_t trackedPid_ = 0;
    std::ofstream csvFile_;
    uint64_t recordedFramesCount_ = 0;
    MetricsConfig metricsConfig_{};
};

} // namespace gnumon::gui
