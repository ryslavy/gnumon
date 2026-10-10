#pragma once

#include "../ipc/FrameRingBuffer.h"
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <dlfcn.h>

namespace gnumon::layer {

class DirectSysfsTelemetry {
public:
    DirectSysfsTelemetry() = default;

    void Sample(ipc::TelemetrySnapshot& snap) {
        auto now = std::chrono::steady_clock::now();
        if (lastSampleTime_.time_since_epoch().count() > 0) {
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSampleTime_).count();
            if (elapsedMs < 100 && cachedSnap_.valid) {
                snap = cachedSnap_;
                return;
            }
        }
        lastSampleTime_ = now;

        SampleGpu(cachedSnap_);
        SampleCpu(cachedSnap_);
        cachedSnap_.valid = 1;
        snap = cachedSnap_;
    }

private:
    static double ReadSysfsDouble(const std::filesystem::path& path, double divisor = 1.0) {
        std::ifstream f(path);
        if (!f.is_open()) return 0.0;
        double val = 0.0;
        f >> val;
        return val / divisor;
    }

    static uint64_t ReadSysfsUint64(const std::filesystem::path& path) {
        std::ifstream f(path);
        if (!f.is_open()) return 0;
        uint64_t val = 0;
        f >> val;
        return val;
    }

    void EnsureGpuPaths() {
        if (gpuPathsInitialized_) return;
        gpuPathsInitialized_ = true;

        for (int i = 0; i < 8; ++i) {
            auto dev = std::filesystem::path("/sys/class/drm") / ("card" + std::to_string(i)) / "device";
            auto vendorPath = dev / "vendor";
            std::ifstream vf(vendorPath);
            if (!vf.is_open()) continue;
            std::string vendor;
            vf >> vendor;

            if (vendor == "0x1002") { // AMD
                gpuCardPath_ = dev;
                gpuVendor_ = 1;
                auto hwmonDir = dev / "hwmon";
                if (std::filesystem::exists(hwmonDir)) {
                    for (const auto& entry : std::filesystem::directory_iterator(hwmonDir)) {
                        if (entry.is_directory() && entry.path().filename().string().rfind("hwmon", 0) == 0) {
                            gpuHwmonPath_ = entry.path();
                            break;
                        }
                    }
                }
                break;
            } else if (vendor == "0x8086") { // Intel
                gpuCardPath_ = dev;
                gpuVendor_ = 2;
                auto hwmonDir = dev / "hwmon";
                if (std::filesystem::exists(hwmonDir)) {
                    for (const auto& entry : std::filesystem::directory_iterator(hwmonDir)) {
                        if (entry.is_directory() && entry.path().filename().string().rfind("hwmon", 0) == 0) {
                            gpuHwmonPath_ = entry.path();
                            break;
                        }
                    }
                }
                break;
            }
        }
    }

    void EnsureCpuPaths() {
        if (cpuPathsInitialized_) return;
        cpuPathsInitialized_ = true;

        // CPU Name from /proc/cpuinfo
        std::ifstream cpuinfo("/proc/cpuinfo");
        if (cpuinfo.is_open()) {
            std::string line;
            while (std::getline(cpuinfo, line)) {
                if (line.rfind("model name", 0) == 0) {
                    auto pos = line.find(':');
                    if (pos != std::string::npos && pos + 2 < line.size()) {
                        cpuName_ = line.substr(pos + 2);
                        break;
                    }
                }
            }
        }
        if (cpuName_.empty()) cpuName_ = "Linux CPU";

        // CPU RAPL Powercap
        std::filesystem::path raplPath = "/sys/class/powercap/intel-rapl/intel-rapl:0/energy_uj";
        if (std::filesystem::exists(raplPath)) {
            cpuRaplPath_ = raplPath;
            prevEnergyUj_ = ReadSysfsUint64(cpuRaplPath_);
            prevEnergyTime_ = std::chrono::steady_clock::now();
        }

        // CPU Temperature Hwmon
        if (std::filesystem::exists("/sys/class/hwmon")) {
            for (const auto& entry : std::filesystem::directory_iterator("/sys/class/hwmon")) {
                auto namePath = entry.path() / "name";
                std::ifstream nf(namePath);
                if (nf.is_open()) {
                    std::string name;
                    nf >> name;
                    if (name == "k10temp" || name == "coretemp" || name == "zenpower" || name == "cpu_thermal") {
                        cpuHwmonPath_ = entry.path();
                        break;
                    }
                }
            }
        }
    }

    void SampleGpu(ipc::TelemetrySnapshot& snap) {
        EnsureGpuPaths();

        if (gpuVendor_ == 1 && !gpuCardPath_.empty()) { // AMD
            snap.gpuUtil = static_cast<float>(ReadSysfsDouble(gpuCardPath_ / "gpu_busy_percent"));
            uint64_t vramUsed = ReadSysfsUint64(gpuCardPath_ / "mem_info_vram_used");
            uint64_t vramTotal = ReadSysfsUint64(gpuCardPath_ / "mem_info_vram_total");
            snap.vramUsedGb = static_cast<float>(static_cast<double>(vramUsed) / (1024.0 * 1024.0 * 1024.0));
            snap.vramTotalGb = static_cast<float>(static_cast<double>(vramTotal) / (1024.0 * 1024.0 * 1024.0));

            if (!gpuHwmonPath_.empty()) {
                snap.gpuTemp = static_cast<float>(ReadSysfsDouble(gpuHwmonPath_ / "temp1_input", 1000.0));
                double power = ReadSysfsDouble(gpuHwmonPath_ / "power1_average", 1'000'000.0);
                if (power <= 0.0) {
                    power = ReadSysfsDouble(gpuHwmonPath_ / "power1_input", 1'000'000.0);
                }
                snap.gpuPower = static_cast<float>(power);
                snap.gpuFreq = static_cast<float>(ReadSysfsDouble(gpuHwmonPath_ / "freq1_input", 1'000'000.0));
                snap.gpuFanSpeed = static_cast<float>(ReadSysfsDouble(gpuHwmonPath_ / "fan1_input"));
                snap.gpuVoltage = static_cast<float>(ReadSysfsDouble(gpuHwmonPath_ / "in0_input"));
            }
        } else if (gpuVendor_ == 2 && !gpuCardPath_.empty()) { // Intel
            if (!gpuHwmonPath_.empty()) {
                snap.gpuTemp = static_cast<float>(ReadSysfsDouble(gpuHwmonPath_ / "temp1_input", 1000.0));
                snap.gpuPower = static_cast<float>(ReadSysfsDouble(gpuHwmonPath_ / "power1_average", 1'000'000.0));
                snap.gpuFreq = static_cast<float>(ReadSysfsDouble(gpuHwmonPath_ / "freq1_input", 1'000'000.0));
            }
        }
    }

    void SampleCpu(ipc::TelemetrySnapshot& snap) {
        EnsureCpuPaths();

        if (snap.cpuName[0] == '\0' && !cpuName_.empty()) {
            std::strncpy(snap.cpuName, cpuName_.c_str(), sizeof(snap.cpuName) - 1);
        }

        // CPU Utilization from /proc/stat
        std::ifstream statFile("/proc/stat");
        if (statFile.is_open()) {
            std::string line;
            if (std::getline(statFile, line) && line.rfind("cpu", 0) == 0) {
                std::istringstream ss(line);
                std::string label;
                uint64_t u = 0, n = 0, s = 0, id = 0, io = 0, ir = 0, sir = 0, st = 0;
                ss >> label >> u >> n >> s >> id >> io >> ir >> sir >> st;
                uint64_t total = u + n + s + id + io + ir + sir + st;
                uint64_t idle = id + io;
                if (prevCpuTotal_ > 0 && total > prevCpuTotal_) {
                    uint64_t totalDelta = total - prevCpuTotal_;
                    uint64_t idleDelta = idle - prevCpuIdle_;
                    if (totalDelta > 0) {
                        float util = 100.0f * (1.0f - static_cast<float>(idleDelta) / static_cast<float>(totalDelta));
                        snap.cpuUtil = std::clamp(util, 0.0f, 100.0f);
                    }
                }
                prevCpuTotal_ = total;
                prevCpuIdle_ = idle;
            }
        }

        // CPU Temp
        if (!cpuHwmonPath_.empty()) {
            double temp = ReadSysfsDouble(cpuHwmonPath_ / "temp1_input", 1000.0);
            if (temp <= 0.0) {
                temp = ReadSysfsDouble(cpuHwmonPath_ / "temp2_input", 1000.0);
            }
            if (temp > 0.0) {
                snap.cpuTemp = static_cast<float>(temp);
            }
        }

        // CPU Power (RAPL)
        if (!cpuRaplPath_.empty()) {
            auto now = std::chrono::steady_clock::now();
            uint64_t curEnergyUj = ReadSysfsUint64(cpuRaplPath_);
            auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(now - prevEnergyTime_).count();
            if (prevEnergyUj_ > 0 && curEnergyUj > prevEnergyUj_ && elapsedNs > 0) {
                double watts = static_cast<double>(curEnergyUj - prevEnergyUj_) / (static_cast<double>(elapsedNs) / 1000.0);
                snap.cpuPower = static_cast<float>(watts);
            }
            prevEnergyUj_ = curEnergyUj;
            prevEnergyTime_ = now;
        }

        // CPU Frequency
        std::ifstream freqFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
        if (freqFile.is_open()) {
            double khz = 0.0;
            freqFile >> khz;
            if (khz > 0.0) snap.cpuFreq = static_cast<float>(khz / 1000.0);
        }
    }

    bool gpuPathsInitialized_ = false;
    int gpuVendor_ = 0; // 1 = AMD, 2 = Intel, 3 = NVIDIA
    std::filesystem::path gpuCardPath_;
    std::filesystem::path gpuHwmonPath_;

    bool cpuPathsInitialized_ = false;
    std::string cpuName_;
    std::filesystem::path cpuHwmonPath_;
    std::filesystem::path cpuRaplPath_;
    uint64_t prevCpuTotal_ = 0;
    uint64_t prevCpuIdle_ = 0;
    uint64_t prevEnergyUj_ = 0;
    std::chrono::steady_clock::time_point prevEnergyTime_{};

    std::chrono::steady_clock::time_point lastSampleTime_{};
    ipc::TelemetrySnapshot cachedSnap_{};
};

} // namespace gnumon::layer
