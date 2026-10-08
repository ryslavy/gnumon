#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include "../../include/gnumon/PresentMonAPI.h"

namespace gnumon::control {

struct GpuMetrics {
    std::string deviceName;
    PM_DEVICE_VENDOR vendor = PM_DEVICE_VENDOR_UNKNOWN;
    uint32_t deviceId = 0;

    // Clocks (MHz)
    double gpuFrequencyMhz = 0.0;
    double memFrequencyMhz = 0.0;

    // Power & Voltage
    double powerWatts = 0.0;
    double voltageMv = 0.0;

    // Temperatures (°C)
    double temperatureEdgeC = 0.0;
    double temperatureHotspotC = 0.0;
    double temperatureMemC = 0.0;

    // Utilization (%)
    double gpuUtilizationPercent = 0.0;
    double memUtilizationPercent = 0.0;

    // Memory (Bytes)
    uint64_t vramTotalBytes = 0;
    uint64_t vramUsedBytes = 0;

    // Fan Speed (RPM)
    double fanSpeedRpm = 0.0;
};

class IGpuTelemetryProvider {
public:
    virtual ~IGpuTelemetryProvider() = default;
    virtual bool Initialize() = 0;
    virtual bool Sample(GpuMetrics& metrics) = 0;
    virtual const std::string& GetName() const = 0;
};

} // namespace gnumon::control
