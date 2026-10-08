#pragma once

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QPoint>
#include <QMouseEvent>
#include <QKeyEvent>
#include <vector>
#include <deque>
#include <string>
#include <chrono>
#include "../../include/gnumon/PresentMonAPI.h"

namespace gnumon::gui {

class PresentMonOverlay : public QWidget {
    Q_OBJECT

public:
    explicit PresentMonOverlay(PM_SESSION_HANDLE session, QWidget *parent = nullptr);
    ~PresentMonOverlay() override;

    void SetTargetProcess(uint32_t pid, const std::string& name);
    void SetRecordingState(bool isRecording, const std::string& csvPath = "");
    void ToggleVisibility();
    void SetCorner(int corner); // 0: Top-Left, 1: Top-Right, 2: Bottom-Left, 3: Bottom-Right

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void OnPollTimer();

private:
    void Reposition();
    void UpdateDynamicQuery();

    PM_SESSION_HANDLE session_ = nullptr;
    PM_DYNAMIC_QUERY_HANDLE query_ = nullptr;
    uint32_t trackedPid_ = 0;
    std::string processName_ = "Auto-Target";

    QTimer *pollTimer_ = nullptr;

    // Metric values
    double displayedFps_ = 0.0;
    double frameTimeMs_ = 0.0;
    double gpuTimeMs_ = 0.0;
    double gpuWaitMs_ = 0.0;

    double gpuPower_ = 0.0;
    double gpuTemp_ = 0.0;
    double gpuUtil_ = 0.0;
    double gpuFreq_ = 0.0;

    double cpuUtil_ = 0.0;
    double cpuPower_ = 0.0;
    double cpuTemp_ = 0.0;
    double cpuFreq_ = 0.0;

    // Recording status
    bool isRecording_ = false;
    std::chrono::steady_clock::time_point recordingStartTime_;
    uint64_t recordedFramesCount_ = 0;

    // Graph history
    struct GraphSample {
        double frameTimeMs = 0.0;
        double gpuTimeMs = 0.0;
    };
    std::deque<GraphSample> history_;
    static constexpr size_t MAX_SAMPLES = 120;

    // UI state
    int corner_ = 0; // Top-Left
    QPoint dragPosition_;
};

} // namespace gnumon::gui
