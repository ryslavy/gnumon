#include "CpuTelemetry.h"
#include "../common/Clock.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace gnumon::control {

double CpuTelemetry::ReadSysfsDouble(const std::filesystem::path& path, double divisor) {
    if (!std::filesystem::exists(path)) return 0.0;
    std::ifstream file(path);
    if (!file.is_open()) return 0.0;
    double val = 0.0;
    file >> val;
    return val / divisor;
}

uint64_t CpuTelemetry::ReadSysfsUint64(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return 0;
    std::ifstream file(path);
    if (!file.is_open()) return 0;
    uint64_t val = 0;
    file >> val;
    return val;
}

bool CpuTelemetry::Initialize() {
    // 1. Detect CPU Name and Vendor from /proc/cpuinfo
    std::ifstream cpuinfo("/proc/cpuinfo");
    if (cpuinfo.is_open()) {
        std::string line;
        while (std::getline(cpuinfo, line)) {
            if (line.rfind("model name", 0) == 0 && cpuName_.empty()) {
                auto pos = line.find(':');
                if (pos != std::string::npos && pos + 2 < line.size()) {
                    cpuName_ = line.substr(pos + 2);
                }
            } else if (line.rfind("vendor_id", 0) == 0 && cpuVendor_ == PM_DEVICE_VENDOR_UNKNOWN) {
                if (line.find("AuthenticAMD") != std::string::npos) {
                    cpuVendor_ = PM_DEVICE_VENDOR_AMD;
                } else if (line.find("GenuineIntel") != std::string::npos) {
                    cpuVendor_ = PM_DEVICE_VENDOR_INTEL;
                }
            } else if (line.rfind("processor", 0) == 0) {
                coreCount_++;
            }
        }
    }

    if (cpuName_.empty()) {
        cpuName_ = "Linux CPU";
    }

    // 2. Detect RAPL energy path for CPU package power
    std::filesystem::path raplCandidate = "/sys/class/powercap/intel-rapl/intel-rapl:0/energy_uj";
    if (std::filesystem::exists(raplCandidate)) {
        raplPath_ = raplCandidate;
        lastRaplEnergyUj_ = ReadSysfsUint64(raplPath_);
    }

    // 3. Detect CPU temperature in /sys/class/hwmon
    if (std::filesystem::exists("/sys/class/hwmon")) {
        for (const auto& entry : std::filesystem::directory_iterator("/sys/class/hwmon")) {
            auto namePath = entry.path() / "name";
            if (std::filesystem::exists(namePath)) {
                std::ifstream nameFile(namePath);
                std::string name;
                nameFile >> name;
                if (name == "k10temp" || name == "coretemp" || name == "zenpower") {
                    cpuHwmonPath_ = entry.path();
                    break;
                }
            }
        }
    }

    // 4. Initial CPU /proc/stat sample (total + per-core)
    std::ifstream statFile("/proc/stat");
    if (statFile.is_open()) {
        std::string line;
        while (std::getline(statFile, line)) {
            if (line.rfind("cpu", 0) == 0) {
                std::istringstream ss(line);
                std::string cpuLabel;
                CpuStatTime t{};
                ss >> cpuLabel >> t.user >> t.nice >> t.system
                   >> t.idle >> t.iowait >> t.irq >> t.softirq >> t.steal;

                if (cpuLabel == "cpu") {
                    lastCpuTime_ = t;
                } else {
                    lastPerCoreTime_.push_back(t);
                }
            }
        }
    }

    lastSampleTimeNs_ = common::Clock::GetTimestampNs();
    isInitialized_ = true;
    return true;
}

bool CpuTelemetry::Sample(CpuMetrics& metrics) {
    if (!isInitialized_ && !Initialize()) {
        return false;
    }

    metrics.cpuName = cpuName_;
    metrics.cpuVendor = cpuVendor_;
    metrics.coreCount = coreCount_;

    uint64_t nowNs = common::Clock::GetTimestampNs();
    uint64_t deltaNs = (nowNs > lastSampleTimeNs_) ? (nowNs - lastSampleTimeNs_) : 1;

    // 1. Read /proc/stat for total and per-core CPU utilization
    std::ifstream statFile("/proc/stat");
    if (statFile.is_open()) {
        std::string line;
        size_t coreIndex = 0;
        metrics.perCoreUtilization.clear();

        while (std::getline(statFile, line)) {
            if (line.rfind("cpu", 0) == 0) {
                std::istringstream ss(line);
                std::string cpuLabel;
                CpuStatTime current{};
                ss >> cpuLabel >> current.user >> current.nice >> current.system
                   >> current.idle >> current.iowait >> current.irq
                   >> current.softirq >> current.steal;

                if (cpuLabel == "cpu") {
                    uint64_t totalDelta = current.GetTotal() - lastCpuTime_.GetTotal();
                    uint64_t activeDelta = current.GetActive() - lastCpuTime_.GetActive();
                    if (totalDelta > 0) {
                        metrics.cpuUtilizationPercent = (static_cast<double>(activeDelta) / static_cast<double>(totalDelta)) * 100.0;
                    }
                    lastCpuTime_ = current;
                } else if (coreIndex < lastPerCoreTime_.size()) {
                    uint64_t coreTotalDelta = current.GetTotal() - lastPerCoreTime_[coreIndex].GetTotal();
                    uint64_t coreActiveDelta = current.GetActive() - lastPerCoreTime_[coreIndex].GetActive();
                    double coreUtil = 0.0;
                    if (coreTotalDelta > 0) {
                        coreUtil = (static_cast<double>(coreActiveDelta) / static_cast<double>(coreTotalDelta)) * 100.0;
                    }
                    metrics.perCoreUtilization.push_back(coreUtil);
                    lastPerCoreTime_[coreIndex] = current;
                    coreIndex++;
                }
            }
        }
    }

    // 2. Read RAPL for CPU Package Power in Watts
    if (!raplPath_.empty()) {
        uint64_t currentEnergyUj = ReadSysfsUint64(raplPath_);
        if (currentEnergyUj >= lastRaplEnergyUj_ && deltaNs > 0) {
            uint64_t deltaUj = currentEnergyUj - lastRaplEnergyUj_;
            metrics.cpuPackagePowerWatts = static_cast<double>(deltaUj * 1'000'000'000ULL) / static_cast<double>(deltaNs * 1'000'000ULL);
        }
        lastRaplEnergyUj_ = currentEnergyUj;
    }

    std::filesystem::path capPath = "/sys/class/powercap/intel-rapl/intel-rapl:0/constraint_0_power_limit_uj";
    if (std::filesystem::exists(capPath)) {
        metrics.cpuPowerLimitWatts = ReadSysfsDouble(capPath, 1'000'000.0);
    }

    // 3. Read CPU Temperature (°C)
    if (!cpuHwmonPath_.empty()) {
        metrics.cpuTemperatureC = ReadSysfsDouble(cpuHwmonPath_ / "temp1_input", 1000.0);

        // Read per-core temperatures if available
        metrics.perCoreTemperature.clear();
        for (int i = 2; i <= 64; ++i) {
            auto inputPath = cpuHwmonPath_ / ("temp" + std::to_string(i) + "_input");
            if (std::filesystem::exists(inputPath)) {
                double coreTemp = ReadSysfsDouble(inputPath, 1000.0);
                metrics.perCoreTemperature.push_back(coreTemp);
            } else {
                break;
            }
        }
    }

    // 4. Read Average CPU Frequency (MHz)
    double freqKhz = ReadSysfsDouble("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    if (freqKhz > 0.0) {
        metrics.cpuFrequencyMhz = freqKhz / 1000.0;
    }

    lastSampleTimeNs_ = nowNs;
    return true;
}

} // namespace gnumon::control
