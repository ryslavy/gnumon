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
                if (recordingActive_) frameConsumer_.SetRecordingActive(true);
                if (inGameOverlayEnabled_) frameConsumer_.SetOverlayEnabled(true);
                return true;
            }
        }
    }

    trackedPid_ = pid;
    if (pid > 0) {
        bool ok = frameConsumer_.Open(pid);
        if (ok) {
            if (recordingActive_) frameConsumer_.SetRecordingActive(true);
            if (inGameOverlayEnabled_) frameConsumer_.SetOverlayEnabled(true);
        }
        return ok;
    }
    return false;
}

void TelemetryCoordinator::StopTrackingProcess(uint32_t /*pid*/) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    trackedPid_ = 0;
    frameConsumer_.Close();
}

void TelemetryCoordinator::RecordFrameLocked(const ipc::FrameEvent& f) {
    if (latestFrame_.frameId > 0) {
        prevFrame_ = latestFrame_;
    }
    latestFrame_ = f;

    if (f.frameTimeNs > 0) {
        double fps = 1'000'000'000.0 / static_cast<double>(f.frameTimeNs);
        double ftMs = static_cast<double>(f.frameTimeNs) / 1'000'000.0;
        fpsHistory_.Push(f.presentStartTimestampNs, fps);
        frameTimeHistory_.Push(f.presentStartTimestampNs, ftMs);
        if (f.frameType != PM_FRAME_TYPE_INTEL_XEFG && f.frameType != PM_FRAME_TYPE_AMD_AFMF && f.frameType != PM_FRAME_TYPE_REPEATED) {
            appFpsHistory_.Push(f.presentStartTimestampNs, fps);
        }
    }

    // Displayed FPS (tracking non-dropped frames)
    if (f.dropped == 0) {
        double dispFps = 0.0;
        if (prevFrame_.displayTimestampNs > 0 && f.displayTimestampNs > prevFrame_.displayTimestampNs) {
            uint64_t dispDeltaNs = f.displayTimestampNs - prevFrame_.displayTimestampNs;
            dispFps = 1'000'000'000.0 / static_cast<double>(dispDeltaNs);
        } else if (f.frameTimeNs > 0) {
            dispFps = 1'000'000'000.0 / static_cast<double>(f.frameTimeNs);
        }
        if (dispFps > 0.0) {
            displayedFpsHistory_.Push(f.displayTimestampNs, dispFps);
        }
    }

    // PC / Render-to-Display Latency
    if (f.displayTimestampNs >= f.cpuStartTimestampNs && f.cpuStartTimestampNs > 0) {
        double latMs = static_cast<double>(f.displayTimestampNs - f.cpuStartTimestampNs) / 1'000'000.0;
        latencyHistory_.Push(f.displayTimestampNs, latMs);
    } else if (f.gpuDurationNs > 0) {
        double latMs = static_cast<double>(f.gpuDurationNs + f.presentDurationNs) / 1'000'000.0;
        latencyHistory_.Push(f.presentStartTimestampNs, latMs);
    }

    // Animation Error: |delta(display) - delta(app/simulation)| in ms
    if (prevFrame_.frameId > 0 && f.displayTimestampNs > prevFrame_.displayTimestampNs) {
        double deltaDispMs = static_cast<double>(f.displayTimestampNs - prevFrame_.displayTimestampNs) / 1'000'000.0;
        double deltaAppMs = (f.cpuStartTimestampNs > prevFrame_.cpuStartTimestampNs && prevFrame_.cpuStartTimestampNs > 0)
            ? (static_cast<double>(f.cpuStartTimestampNs - prevFrame_.cpuStartTimestampNs) / 1'000'000.0)
            : (static_cast<double>(f.frameTimeNs) / 1'000'000.0);
        double animErrMs = std::abs(deltaDispMs - deltaAppMs);
        animErrorHistory_.Push(f.displayTimestampNs, animErrMs);
    }
}

void TelemetryCoordinator::EnsureConsumerConnectedLocked() {
    if (frameConsumer_.IsConnected()) {
        uint32_t currentPid = frameConsumer_.GetProcessId();
        bool isDead = (currentPid > 0 && !std::filesystem::exists("/proc/" + std::to_string(currentPid)));
        bool targetChanged = (trackedPid_ > 0 && trackedPid_ != currentPid);
        if (isDead || targetChanged) {
            frameConsumer_.Close();
        }
    }

    if (!frameConsumer_.IsConnected()) {
        uint32_t target = trackedPid_;
        // If trackedPid_ is 0 or its ring doesn't exist, search active rings
        if (target == 0 || !std::filesystem::exists("/dev/shm/gnumon_ring_" + std::to_string(target))) {
            auto active = common::GetActiveRingPids();
            if (!active.empty()) {
                target = active.front();
                trackedPid_ = target;
            }
        }
        if (target > 0) {
            if (frameConsumer_.Open(target)) {
                if (recordingActive_) {
                    frameConsumer_.SetRecordingActive(true);
                }
                if (inGameOverlayEnabled_) {
                    frameConsumer_.SetOverlayEnabled(true);
                }
            }
        }
    }
}

bool TelemetryCoordinator::GetLatestFrame(ipc::FrameEvent& outEvent) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    EnsureConsumerConnectedLocked();

    ipc::FrameEvent event{};
    bool hasNew = false;

    // Drain to latest frame and preserve every frame in the consumer queue
    while (frameConsumer_.Pop(event)) {
        RecordFrameLocked(event);
        frameEventQueue_.push_back(event);
        if (frameEventQueue_.size() > 8192) {
            frameEventQueue_.pop_front();
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
    EnsureConsumerConnectedLocked();

    // Drain any remaining frames into the FIFO queue
    ipc::FrameEvent event{};
    while (frameConsumer_.Pop(event)) {
        RecordFrameLocked(event);
        frameEventQueue_.push_back(event);
        if (frameEventQueue_.size() > 8192) {
            frameEventQueue_.pop_front();
        }
    }

    if (!frameEventQueue_.empty()) {
        outEvent = frameEventQueue_.front();
        frameEventQueue_.pop_front();
        return true;
    }
    return false;
}

void TelemetryCoordinator::SetRecordingState(bool active) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    recordingActive_ = active;
    EnsureConsumerConnectedLocked();
    if (frameConsumer_.IsConnected()) {
        frameConsumer_.SetRecordingActive(active);
    }
}

bool TelemetryCoordinator::IsRecordingActive() const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (const_cast<TelemetryCoordinator*>(this)->frameConsumer_.IsConnected()) {
        const_cast<TelemetryCoordinator*>(this)->recordingActive_ = const_cast<TelemetryCoordinator*>(this)->frameConsumer_.IsRecordingActive();
    }
    return recordingActive_;
}

double TelemetryCoordinator::GetStatisticalMetric(PM_METRIC metric, PM_STAT stat, double windowSizeMs) {
    std::lock_guard<std::mutex> lock(dataMutex_);

    uint64_t nowNs = common::Clock::GetTimestampNs();
    uint64_t windowNs = static_cast<uint64_t>(windowSizeMs * 1'000'000.0);
    uint64_t cutoffNs = (nowNs > windowNs) ? (nowNs - windowNs) : 0;

    fpsHistory_.PruneOlderThan(cutoffNs);
    displayedFpsHistory_.PruneOlderThan(cutoffNs);
    appFpsHistory_.PruneOlderThan(cutoffNs);
    frameTimeHistory_.PruneOlderThan(cutoffNs);
    latencyHistory_.PruneOlderThan(cutoffNs);
    animErrorHistory_.PruneOlderThan(cutoffNs);

    const common::SlidingStatistics* targetStats = nullptr;
    if (metric == PM_METRIC_DISPLAYED_FPS) {
        targetStats = &displayedFpsHistory_;
    } else if (metric == PM_METRIC_APPLICATION_FPS) {
        targetStats = &appFpsHistory_;
    } else if (metric == PM_METRIC_PRESENTED_FPS) {
        targetStats = &fpsHistory_;
    } else if (metric == PM_METRIC_CPU_FRAME_TIME || metric == PM_METRIC_DISPLAYED_FRAME_TIME || metric == PM_METRIC_PRESENTED_FRAME_TIME) {
        targetStats = &frameTimeHistory_;
    } else if (metric == PM_METRIC_DISPLAY_LATENCY || metric == PM_METRIC_PC_LATENCY || metric == PM_METRIC_RENDER_PRESENT_LATENCY || metric == PM_METRIC_UNTIL_DISPLAYED) {
        targetStats = &latencyHistory_;
    } else if (metric == PM_METRIC_ANIMATION_ERROR) {
        targetStats = &animErrorHistory_;
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

        EnsureConsumerConnectedLocked();
        if (frameConsumer_.IsConnected()) {
            ipc::TelemetrySnapshot snap{};
            snap.gpuUtil = static_cast<float>(gpu.gpuUtilizationPercent);
            snap.gpuTemp = static_cast<float>(gpu.temperatureEdgeC);
            snap.gpuPower = static_cast<float>(gpu.powerWatts);
            snap.gpuFreq = static_cast<float>(gpu.gpuFrequencyMhz);
            snap.vramUsedGb = static_cast<float>(static_cast<double>(gpu.vramUsedBytes) / (1024.0 * 1024.0 * 1024.0));
            snap.vramTotalGb = static_cast<float>(static_cast<double>(gpu.vramTotalBytes) / (1024.0 * 1024.0 * 1024.0));
            snap.cpuUtil = static_cast<float>(cpu.cpuUtilizationPercent);
            snap.cpuTemp = static_cast<float>(cpu.cpuTemperatureC);
            snap.cpuPower = static_cast<float>(cpu.cpuPackagePowerWatts);
            snap.cpuFreq = static_cast<float>(cpu.cpuFrequencyMhz);
            snap.gpuVoltage = static_cast<float>(gpu.voltageMv);
            snap.gpuFanSpeed = static_cast<float>(gpu.fanSpeedRpm);
            if (!gpu.deviceName.empty()) {
                std::strncpy(snap.gpuName, gpu.deviceName.c_str(), sizeof(snap.gpuName) - 1);
            }
            if (!cpu.cpuName.empty()) {
                std::strncpy(snap.cpuName, cpu.cpuName.c_str(), sizeof(snap.cpuName) - 1);
            }
            snap.valid = 1;
            frameConsumer_.WriteTelemetry(snap);
        }
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

bool TelemetryCoordinator::ConsumeInGameHudHotkeyToggle() {
    bool triggered = inputTracker_.ConsumeInGameHudHotkeyToggle();
    if (!triggered) {
        std::lock_guard<std::mutex> lock(dataMutex_);
        EnsureConsumerConnectedLocked();
        if (frameConsumer_.IsConnected()) {
            bool ringHud = frameConsumer_.IsOverlayEnabled();
            if (ringHud != inGameOverlayEnabled_) {
                triggered = true;
            }
        }
    }
    return triggered;
}

bool TelemetryCoordinator::ConsumeOverlayHotkeyToggle() {
    return inputTracker_.ConsumeOverlayHotkeyToggle();
}

bool TelemetryCoordinator::ConsumeRecordHotkeyToggle() {
    bool triggered = inputTracker_.ConsumeRecordHotkeyToggle();
    if (!triggered) {
        std::lock_guard<std::mutex> lock(dataMutex_);
        EnsureConsumerConnectedLocked();
        if (frameConsumer_.IsConnected()) {
            bool ringRec = frameConsumer_.IsRecordingActive();
            if (ringRec != recordingActive_) {
                triggered = true;
            }
        }
    }
    return triggered;
}

bool TelemetryCoordinator::ConsumeMiniHudHotkeyToggle() {
    return inputTracker_.ConsumeMiniHudHotkeyToggle();
}

void TelemetryCoordinator::SetHotkeys(const std::string& inGameHud, const std::string& record,
                                      const std::string& overlay, const std::string& miniHud)
{
    inputTracker_.SetHotkeys(inGameHud, record, overlay, miniHud);
}

void TelemetryCoordinator::SetInGameOverlayEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    inGameOverlayEnabled_ = enabled;
    EnsureConsumerConnectedLocked();
    if (frameConsumer_.IsConnected()) {
        frameConsumer_.SetOverlayEnabled(enabled);
    }
}

bool TelemetryCoordinator::IsInGameOverlayEnabled() const {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (const_cast<TelemetryCoordinator*>(this)->frameConsumer_.IsConnected()) {
        const_cast<TelemetryCoordinator*>(this)->inGameOverlayEnabled_ = const_cast<TelemetryCoordinator*>(this)->frameConsumer_.IsOverlayEnabled();
    }
    return inGameOverlayEnabled_;
}

} // namespace gnumon::service
