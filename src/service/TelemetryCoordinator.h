#pragma once

#include "../control/GpuTelemetry.h"
#include "../control/AmdGpuTelemetry.h"
#include "../control/NvmlGpuTelemetry.h"
#include "../control/IntelGpuTelemetry.h"
#include "../control/CpuTelemetry.h"
#include "../control/InputLatencyTracker.h"
#include "../ipc/FrameRingBuffer.h"
#include "../common/SlidingStatistics.h"
#include <memory>
#include <mutex>

namespace gnumon::service {

class TelemetryCoordinator {
public:
    TelemetryCoordinator();
    ~TelemetryCoordinator() = default;

    bool Initialize();
    void SampleAll();

    bool StartTrackingProcess(uint32_t pid);
    void StopTrackingProcess(uint32_t pid);
    bool GetLatestFrame(ipc::FrameEvent& outEvent);
    bool PopFrame(ipc::FrameEvent& outEvent);
    double GetStatisticalMetric(PM_METRIC metric, PM_STAT stat, double windowSizeMs);

    control::GpuMetrics GetLatestGpuMetrics() const;
    control::CpuMetrics GetLatestCpuMetrics() const;
    uint64_t GetLastClickTimestampNs() const;
    bool ConsumeHotkeyToggle();
    bool ConsumeInGameHudHotkeyToggle();
    bool ConsumeOverlayHotkeyToggle();
    bool ConsumeRecordHotkeyToggle();
    bool ConsumeMiniHudHotkeyToggle();
    void SetHotkeys(const std::string& inGameHud, const std::string& record,
                    const std::string& overlay, const std::string& miniHud);

    void SetInGameOverlayEnabled(bool enabled);
    bool IsInGameOverlayEnabled() const;

private:
    std::unique_ptr<control::IGpuTelemetryProvider> gpuProvider_;
    control::CpuTelemetry cpuTelemetry_;
    control::InputLatencyTracker inputTracker_;
    ipc::FrameRingConsumer frameConsumer_;
    uint32_t trackedPid_ = 0;

    mutable std::mutex dataMutex_;
    control::GpuMetrics latestGpuMetrics_{};
    control::CpuMetrics latestCpuMetrics_{};
    ipc::FrameEvent latestFrame_{};
    ipc::FrameEvent prevFrame_{};
    common::SlidingStatistics fpsHistory_;
    common::SlidingStatistics displayedFpsHistory_;
    common::SlidingStatistics frameTimeHistory_;
    common::SlidingStatistics latencyHistory_;
    common::SlidingStatistics animErrorHistory_;
};

} // namespace gnumon::service
