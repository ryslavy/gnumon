#include "IntelGpuTelemetry.h"
#include <fstream>
#include <iostream>

namespace gnumon::control {

double IntelGpuTelemetry::ReadSysfsDouble(const std::filesystem::path& path, double divisor) {
    if (!std::filesystem::exists(path)) return 0.0;
    std::ifstream file(path);
    if (!file.is_open()) return 0.0;
    double val = 0.0;
    file >> val;
    return val / divisor;
}

uint64_t IntelGpuTelemetry::ReadSysfsUint64(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return 0;
    std::ifstream file(path);
    if (!file.is_open()) return 0;
    uint64_t val = 0;
    file >> val;
    return val;
}

std::string IntelGpuTelemetry::ReadSysfsString(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return "";
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::string val;
    std::getline(file, val);
    return val;
}

bool IntelGpuTelemetry::Initialize() {
    // Scan for Intel GPU in /sys/class/drm/card*
    for (int i = 0; i < 8; ++i) {
        auto candidate = std::filesystem::path("/sys/class/drm") / ("card" + std::to_string(i));
        auto devicePath = candidate / "device";
        auto vendorPath = devicePath / "vendor";

        if (std::filesystem::exists(vendorPath)) {
            std::string vendor = ReadSysfsString(vendorPath);
            // 0x8086 is Intel vendor ID
            if (vendor == "0x8086") {
                cardPath_ = candidate;

                // Check for frequency node
                if (std::filesystem::exists(candidate / "gt_act_freq_mhz")) {
                    freqPath_ = candidate / "gt_act_freq_mhz";
                } else if (std::filesystem::exists(candidate / "gt/gt0/act_freq_mhz")) {
                    freqPath_ = candidate / "gt/gt0/act_freq_mhz";
                } else if (std::filesystem::exists(candidate / "gt/gt0/rps_act_freq_mhz")) {
                    freqPath_ = candidate / "gt/gt0/rps_act_freq_mhz";
                }

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

                // Try reading device label/model
                auto labelPath = devicePath / "label";
                if (std::filesystem::exists(labelPath)) {
                    std::string label = ReadSysfsString(labelPath);
                    if (!label.empty()) {
                        gpuModelName_ = label;
                    }
                } else {
                    gpuModelName_ = "Intel Arc / Iris Xe Graphics";
                }

                isInitialized_ = true;
                return true;
            }
        }
    }
    return false;
}

bool IntelGpuTelemetry::Sample(GpuMetrics& metrics) {
    if (!isInitialized_ && !Initialize()) {
        return false;
    }

    metrics.vendor = PM_DEVICE_VENDOR_INTEL;
    metrics.deviceName = gpuModelName_;

    // Frequency
    if (!freqPath_.empty()) {
        metrics.gpuFrequencyMhz = ReadSysfsDouble(freqPath_);
    }

    // hwmon telemetry (temperatures, power, fan, voltage)
    if (!hwmonPath_.empty()) {
        // Temperature: temp1_input in millidegrees C -> C
        metrics.temperatureEdgeC = ReadSysfsDouble(hwmonPath_ / "temp1_input", 1000.0);

        // Power: power1_average or power1_input in microwatts -> Watts
        double pwr = ReadSysfsDouble(hwmonPath_ / "power1_average", 1'000'000.0);
        if (pwr <= 0.0) {
            pwr = ReadSysfsDouble(hwmonPath_ / "power1_input", 1'000'000.0);
        }
        metrics.powerWatts = pwr;

        // Fan RPM
        metrics.fanSpeedRpm = ReadSysfsDouble(hwmonPath_ / "fan1_input");

        // Voltage: in0_input in mV
        metrics.voltageMv = ReadSysfsDouble(hwmonPath_ / "in0_input");
    }

    // VRAM / Local memory
    if (std::filesystem::exists(cardPath_ / "lmem_total_bytes")) {
        metrics.vramTotalBytes = ReadSysfsUint64(cardPath_ / "lmem_total_bytes");
        uint64_t avail = ReadSysfsUint64(cardPath_ / "lmem_avail_bytes");
        if (metrics.vramTotalBytes >= avail) {
            metrics.vramUsedBytes = metrics.vramTotalBytes - avail;
        }
    }

    return true;
}

} // namespace gnumon::control
