#pragma once

#include <cstdint>
#include <string>
#include <filesystem>
#include <vector>
#include "../../include/gnumon/PresentMonAPI.h"

namespace gnumon::control {

struct CpuMetrics {
    std::string cpuName;
    PM_DEVICE_VENDOR cpuVendor = PM_DEVICE_VENDOR_UNKNOWN;
    uint32_t coreCount = 0;
    double cpuUtilizationPercent = 0.0;
    double cpuPackagePowerWatts = 0.0;
    double cpuPowerLimitWatts = 0.0;
    double cpuTemperatureC = 0.0;
    double cpuFrequencyMhz = 0.0;
    std::vector<double> perCoreUtilization;
    std::vector<double> perCoreTemperature;
};

class CpuTelemetry {
public:
    CpuTelemetry() = default;
    ~CpuTelemetry() = default;

    bool Initialize();
    bool Sample(CpuMetrics& metrics);

private:
    struct CpuStatTime {
        uint64_t user = 0;
        uint64_t nice = 0;
        uint64_t system = 0;
        uint64_t idle = 0;
        uint64_t iowait = 0;
        uint64_t irq = 0;
        uint64_t softirq = 0;
        uint64_t steal = 0;

        uint64_t GetTotal() const {
            return user + nice + system + idle + iowait + irq + softirq + steal;
        }
        uint64_t GetActive() const {
            return user + nice + system + irq + softirq + steal;
        }
    };

    CpuStatTime lastCpuTime_{};
    std::vector<CpuStatTime> lastPerCoreTime_;
    uint64_t lastRaplEnergyUj_ = 0;
    uint64_t lastSampleTimeNs_ = 0;

    std::string cpuName_;
    PM_DEVICE_VENDOR cpuVendor_ = PM_DEVICE_VENDOR_UNKNOWN;
    uint32_t coreCount_ = 0;

    std::filesystem::path raplPath_;
    std::filesystem::path cpuHwmonPath_;
    bool isInitialized_ = false;

    static double ReadSysfsDouble(const std::filesystem::path& path, double divisor = 1.0);
    static uint64_t ReadSysfsUint64(const std::filesystem::path& path);
};

} // namespace gnumon::control
