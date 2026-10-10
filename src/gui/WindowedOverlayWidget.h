#pragma once

#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QPoint>
#include <deque>
#include <vector>
#include <string>
#include "AppConfig.h"
#include "../ipc/FrameRingBuffer.h"
#include <gnumon/PresentMonAPI.h>

namespace gnumon::gui {

class WindowedOverlayWidget : public QWidget {
    Q_OBJECT

public:
    explicit WindowedOverlayWidget(AppConfig *config, QWidget *parent = nullptr);
    ~WindowedOverlayWidget() override = default;

    void UpdateMetrics(double fps, double displayedFps, double frameTimeMs, double untilDisplayedMs,
                       double dropped, double animErrorMs, const ipc::TelemetrySnapshot* telem);
    void UpdateFromSnapshot(const PM_FULL_TELEMETRY_SNAPSHOT& snap);

    void ReloadLayout();

signals:
    void windowedClosed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    float CalculateStat(const std::deque<float>& q, int statId);
    const std::deque<float>& GetHistoryForMetric(int metricId);
    void PushSample(std::deque<float>& q, float val, size_t maxLen = 120);
    QString GetStringMetricValue(int metricId) const;
    bool IsStringMetric(int metricId) const;

    AppConfig *config_ = nullptr;
    bool dragging_ = false;
    QPoint dragPosition_;

    // Metric history buffers (matching in-game HUD)
    std::deque<float> frametimes_;
    std::deque<float> histUntilDisplayed_;
    std::deque<float> histDroppedFrames_;
    std::deque<float> histFps_;
    std::deque<float> histAnimError_;
    std::deque<float> histGpuTime_;
    std::deque<float> histGpuBusy_;
    std::deque<float> histGpuWait_;
    std::deque<float> histCpuBusy_;
    std::deque<float> histCpuWait_;
    std::deque<float> histGpuUtil_;
    std::deque<float> histGpuPower_;
    std::deque<float> histGpuTemp_;
    std::deque<float> histGpuFreq_;
    std::deque<float> histGpuVoltage_;
    std::deque<float> histGpuFanSpeed_;
    std::deque<float> histVramUsed_;
    std::deque<float> histCpuUtil_;
    std::deque<float> histCpuPower_;
    std::deque<float> histCpuTemp_;
    std::deque<float> histCpuFreq_;

    QString gpuName_ = "Auto-detect GPU";
    QString cpuName_ = "Linux CPU";
    QString gpuVendor_ = "GPU";
    QString cpuVendor_ = "CPU";
    QString appName_ = "Active App";
};

} // namespace gnumon::gui
