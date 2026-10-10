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

                // Read device ID (e.g. 0x73ff)
                auto devIdPath = devicePath / "device";
                if (std::filesystem::exists(devIdPath)) {
                    std::string devHex = ReadSysfsString(devIdPath);
                    try {
                        deviceId_ = std::stoul(devHex, nullptr, 16);
                    } catch (...) {}

                    if (devHex.rfind("0x", 0) == 0) devHex = devHex.substr(2);
                    for (char& c : devHex) c = std::tolower(c);

                    // Look up commercial model name in pci.ids
                    const char* pciPaths[] = {"/usr/share/hwdata/pci.ids", "/usr/share/misc/pci.ids"};
                    for (const char* pciPath : pciPaths) {
                        std::ifstream file(pciPath);
                        if (!file.is_open()) continue;

                        std::string line;
                        bool inAmd = false;
                        while (std::getline(file, line)) {
                            if (line.empty() || line[0] == '#') continue;
                            if (line.rfind("1002  ", 0) == 0) {
                                inAmd = true;
                                continue;
                            }
                            if (inAmd) {
                                if (line[0] != '\t') break; // left AMD vendor block
                                if (line.size() > 6 && line[0] == '\t' && line[1] != '\t') {
                                    std::string id = line.substr(1, 4);
                                    for (char& c : id) c = std::tolower(c);
                                    if (id == devHex) {
                                        size_t start = line.find_first_not_of(" \t", 5);
                                        if (start != std::string::npos) {
                                            std::string name = line.substr(start);
                                            auto bOpen = name.find('[');
                                            auto bClose = name.find(']', bOpen);
                                            if (bOpen != std::string::npos && bClose != std::string::npos && bClose > bOpen + 1) {
                                                gpuModelName_ = "AMD " + name.substr(bOpen + 1, bClose - bOpen - 1);
                                            } else {
                                                gpuModelName_ = "AMD " + name;
                                            }
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                        if (gpuModelName_ != "AMD Radeon Graphics") break;
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
    metrics.deviceId = deviceId_;
    metrics.deviceName = gpuModelName_;

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
