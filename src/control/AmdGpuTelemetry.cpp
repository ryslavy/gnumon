#include "AmdGpuTelemetry.h"
#include <fstream>
#include <iostream>

namespace gnumon::control {

double AmdGpuTelemetry::ReadSysfsDouble(const std::filesystem::path& path, double divisor) {
    if (!std::filesystem::exists(path)) return 0.0;
    std::ifstream file(path);
    if (!file.is_open()) return 0.0;
    double val = 0.0;
    file >> val;
    return val / divisor;
}

uint64_t AmdGpuTelemetry::ReadSysfsUint64(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return 0;
    std::ifstream file(path);
    if (!file.is_open()) return 0;
    uint64_t val = 0;
    file >> val;
    return val;
}

std::string AmdGpuTelemetry::ReadSysfsString(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return "";
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::string val;
    std::getline(file, val);
    return val;
}

bool AmdGpuTelemetry::Initialize() {
    // Scan for AMD GPU in /sys/class/drm/card*
    for (int i = 0; i < 8; ++i) {
        auto candidate = std::filesystem::path("/sys/class/drm") / ("card" + std::to_string(i));
        auto devicePath = candidate / "device";
        auto vendorPath = devicePath / "vendor";

        if (std::filesystem::exists(vendorPath)) {
            std::string vendor = ReadSysfsString(vendorPath);
            // 0x1002 is AMD/ATI vendor ID
            if (vendor == "0x1002") {
                cardPath_ = devicePath;

                // Look for hwmon subdirectory
                auto hwmonDir = devicePath / "hwmon";
                if (std::filesystem::exists(hwmonDir)) {
                    for (const auto& entry : std::filesystem::directory_iterator(hwmonDir)) {
                        if (entry.is_directory() && entry.path().filename().string().rfind("hwmon", 0) == 0) {
                            hwmonPath_ = entry.path();
                            break;
                        }
                    }
                }

                isInitialized_ = true;
                return true;
            }
        }
    }
    return false;
}

bool AmdGpuTelemetry::Sample(GpuMetrics& metrics) {
    if (!isInitialized_ && !Initialize()) {
        return false;
    }

    metrics.vendor = PM_DEVICE_VENDOR_AMD;
    metrics.deviceName = "AMD Radeon Graphics";

    // Utilization (%)
    metrics.gpuUtilizationPercent = ReadSysfsDouble(cardPath_ / "gpu_busy_percent");
    metrics.memUtilizationPercent = ReadSysfsDouble(cardPath_ / "mem_busy_percent");

    // VRAM (Bytes)
    metrics.vramUsedBytes = ReadSysfsUint64(cardPath_ / "mem_info_vram_used");
    metrics.vramTotalBytes = ReadSysfsUint64(cardPath_ / "mem_info_vram_total");

    if (!hwmonPath_.empty()) {
        // Clocks: freq1_input is in Hz -> MHz
        metrics.gpuFrequencyMhz = ReadSysfsDouble(hwmonPath_ / "freq1_input", 1'000'000.0);
        metrics.memFrequencyMhz = ReadSysfsDouble(hwmonPath_ / "freq2_input", 1'000'000.0);

        // Power: power1_average is in microWatts -> Watts
        metrics.powerWatts = ReadSysfsDouble(hwmonPath_ / "power1_average", 1'000'000.0);
        if (std::filesystem::exists(hwmonPath_ / "power1_cap")) {
            metrics.powerLimitWatts = ReadSysfsDouble(hwmonPath_ / "power1_cap", 1'000'000.0);
        }

        // Temperatures: milliCelsius -> Celsius
        metrics.temperatureEdgeC = ReadSysfsDouble(hwmonPath_ / "temp1_input", 1000.0);
        metrics.temperatureHotspotC = ReadSysfsDouble(hwmonPath_ / "temp2_input", 1000.0);
        metrics.temperatureMemC = ReadSysfsDouble(hwmonPath_ / "temp3_input", 1000.0);

        // Fan RPM
        metrics.fanSpeedRpm = ReadSysfsDouble(hwmonPath_ / "fan1_input");

        // Voltage (mV)
        metrics.voltageMv = ReadSysfsDouble(hwmonPath_ / "in0_input");
    }

    return true;
}

} // namespace gnumon::control
