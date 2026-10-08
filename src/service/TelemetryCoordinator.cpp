#include "TelemetryCoordinator.h"
#include "../common/Clock.h"
#include "../common/ProcUtils.h"
#include <iostream>

namespace gnumon::service {

TelemetryCoordinator::TelemetryCoordinator() = default;

bool TelemetryCoordinator::Initialize() {
    bool gpuOk = false;

    // Try AMD
    auto amd = std::make_unique<control::AmdGpuTelemetry>();
    if (amd->Initialize()) {
        gpuProvider_ = std::move(amd);
        gpuOk = true;
    }

    // Try NVIDIA NVML if no AMD or AMD init failed
    if (!gpuOk) {
        auto nvml = std::make_unique<control::NvmlGpuTelemetry>();
        if (nvml->Initialize()) {
            gpuProvider_ = std::move(nvml);
            gpuOk = true;
        }
    }

    // Try Intel DRM / sysfs if still not found
    if (!gpuOk) {
        auto intel = std::make_unique<control::IntelGpuTelemetry>();
        if (intel->Initialize()) {
            gpuProvider_ = std::move(intel);
            gpuOk = true;
        }
    }

    bool cpuOk = cpuTelemetry_.Initialize();
    inputTracker_.Start();
    return gpuOk || cpuOk;
}

bool TelemetryCoordinator::StartTrackingProcess(uint32_t pid) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    frameConsumer_.Close();

    // If pid == 0, autotarget any live running game ring
    if (pid == 0) {
        auto active = common::GetActiveRingPids();
        if (!active.empty()) {
            pid = active.front();
        }
    } else {
        // Resolve thread ID (TID) to process ID (TGID)
        uint32_t tgid = common::GetTgidForPid(pid);
        if (tgid > 0 && tgid != pid) {
            if (frameConsumer_.Open(tgid)) {
                trackedPid_ = tgid;
                return true;
            }
        }
    }

    trackedPid_ = pid;
    if (pid > 0) {
        return frameConsumer_.Open(pid);
    }
    return false;
}

void TelemetryCoordinator::StopTrackingProcess(uint32_t /*pid*/) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    trackedPid_ = 0;
    frameConsumer_.Close();
}

bool TelemetryCoordinator::GetLatestFrame(ipc::FrameEvent& outEvent) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (!frameConsumer_.IsConnected()) {
        uint32_t target = trackedPid_;
        if (target == 0) {
            auto active = common::GetActiveRingPids();
            if (!active.empty()) target = active.front();
        }
        if (target > 0) {
            frameConsumer_.Open(target);
        }
    }

    ipc::FrameEvent event{};
    bool hasNew = false;
    // Drain to latest frame
    while (frameConsumer_.Pop(event)) {
        latestFrame_ = event;
        if (event.frameTimeNs > 0) {
            double fps = 1'000'000'000.0 / static_cast<double>(event.frameTimeNs);
            double ftMs = static_cast<double>(event.frameTimeNs) / 1'000'000.0;
            fpsHistory_.Push(event.presentStartTimestampNs, fps);
            frameTimeHistory_.Push(event.presentStartTimestampNs, ftMs);
        }
        hasNew = true;
    }

    if (hasNew || latestFrame_.frameId > 0) {
        outEvent = latestFrame_;
        return true;
    }
    return false;
}

bool TelemetryCoordinator::PopFrame(ipc::FrameEvent& outEvent) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (!frameConsumer_.IsConnected() && trackedPid_ > 0) {
        frameConsumer_.Open(trackedPid_);
    }

    if (frameConsumer_.Pop(outEvent)) {
        latestFrame_ = outEvent;
        if (outEvent.frameTimeNs > 0) {
            double fps = 1'000'000'000.0 / static_cast<double>(outEvent.frameTimeNs);
            double ftMs = static_cast<double>(outEvent.frameTimeNs) / 1'000'000.0;
            fpsHistory_.Push(outEvent.presentStartTimestampNs, fps);
            frameTimeHistory_.Push(outEvent.presentStartTimestampNs, ftMs);
        }
        return true;
    }
    return false;
}

double TelemetryCoordinator::GetStatisticalMetric(PM_METRIC metric, PM_STAT stat, double windowSizeMs) {
    std::lock_guard<std::mutex> lock(dataMutex_);

    uint64_t nowNs = common::Clock::GetTimestampNs();
    uint64_t windowNs = static_cast<uint64_t>(windowSizeMs * 1'000'000.0);
    uint64_t cutoffNs = (nowNs > windowNs) ? (nowNs - windowNs) : 0;

    fpsHistory_.PruneOlderThan(cutoffNs);
    frameTimeHistory_.PruneOlderThan(cutoffNs);

    const common::SlidingStatistics* targetStats = nullptr;
    if (metric == PM_METRIC_DISPLAYED_FPS || metric == PM_METRIC_PRESENTED_FPS || metric == PM_METRIC_APPLICATION_FPS) {
        targetStats = &fpsHistory_;
    } else if (metric == PM_METRIC_CPU_FRAME_TIME || metric == PM_METRIC_DISPLAYED_FRAME_TIME || metric == PM_METRIC_PRESENTED_FRAME_TIME) {
        targetStats = &frameTimeHistory_;
    }

    if (!targetStats || targetStats->IsEmpty()) {
        return 0.0;
    }

    switch (stat) {
    case PM_STAT_NONE:
    case PM_STAT_AVG:
        return targetStats->GetAverage();
    case PM_STAT_MIN:
        return targetStats->GetMin();
    case PM_STAT_MAX:
        return targetStats->GetMax();
    case PM_STAT_PERCENTILE_99:
        return targetStats->Get99thPercentile();
    case PM_STAT_PERCENTILE_95:
        return targetStats->Get95thPercentile();
    case PM_STAT_PERCENTILE_90:
        return targetStats->GetPercentile(90.0);
    case PM_STAT_PERCENTILE_01:
        return targetStats->GetPercentile(1.0);
    case PM_STAT_PERCENTILE_05:
        return targetStats->GetPercentile(5.0);
    case PM_STAT_PERCENTILE_10:
        return targetStats->GetPercentile(10.0);
    case PM_STAT_COUNT:
        return static_cast<double>(targetStats->GetCount());
    default:
        return targetStats->GetAverage();
    }
}

void TelemetryCoordinator::SampleAll() {
    control::GpuMetrics gpu{};
    if (gpuProvider_) {
        gpuProvider_->Sample(gpu);
    }

    control::CpuMetrics cpu{};
    cpuTelemetry_.Sample(cpu);

    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        latestGpuMetrics_ = gpu;
        latestCpuMetrics_ = cpu;
    }
}

control::GpuMetrics TelemetryCoordinator::GetLatestGpuMetrics() const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    return latestGpuMetrics_;
}

control::CpuMetrics TelemetryCoordinator::GetLatestCpuMetrics() const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    return latestCpuMetrics_;
}

uint64_t TelemetryCoordinator::GetLastClickTimestampNs() const {
    return inputTracker_.GetLastClickTimestampNs();
}

bool TelemetryCoordinator::ConsumeHotkeyToggle() {
    return inputTracker_.ConsumeHotkeyToggle();
}

bool TelemetryCoordinator::ConsumeOverlayHotkeyToggle() {
    return inputTracker_.ConsumeOverlayHotkeyToggle();
}

bool TelemetryCoordinator::ConsumeRecordHotkeyToggle() {
    return inputTracker_.ConsumeRecordHotkeyToggle();
}

} // namespace gnumon::service
