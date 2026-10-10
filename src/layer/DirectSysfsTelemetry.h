#pragma once

#include "../ipc/FrameRingBuffer.h"
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <algorithm>
#include <mutex>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

namespace gnumon::layer {

class DirectSysfsTelemetry {
public:
    DirectSysfsTelemetry() = default;

    void Sample(ipc::TelemetrySnapshot& snap) {
        std::lock_guard<std::mutex> lock(mutex_);
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
    static double ReadSysfsDouble(const char* path, double divisor = 1.0) {
        if (!path || path[0] == '\0') return 0.0;
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) return 0.0;
        char buf[64]{};
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) return 0.0;
        buf[n] = '\0';
        char* end = nullptr;
        double val = strtod(buf, &end);
        return (divisor > 0.0) ? (val / divisor) : val;
    }

    static uint64_t ReadSysfsUint64(const char* path) {
        if (!path || path[0] == '\0') return 0;
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) return 0;
        char buf[64]{};
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) return 0;
        buf[n] = '\0';
        char* end = nullptr;
        return strtoull(buf, &end, 10);
    }

    static bool ReadSysfsString(const char* path, char* out, size_t maxLen) {
        if (!path || !out || maxLen == 0) return false;
        out[0] = '\0';
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) return false;
        ssize_t n = read(fd, out, maxLen - 1);
        close(fd);
        if (n <= 0) return false;
        out[n] = '\0';
        while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ')) {
            out[--n] = '\0';
        }
        return true;
    }

    void EnsureGpuPaths() {
        if (gpuPathsInitialized_) return;
        gpuPathsInitialized_ = true;

        for (int i = 0; i < 8; ++i) {
            char vpath[256];
            snprintf(vpath, sizeof(vpath), "/sys/class/drm/card%d/device/vendor", i);
            char vendor[32]{};
            if (!ReadSysfsString(vpath, vendor, sizeof(vendor))) continue;

            if (strcmp(vendor, "0x1002") == 0) { // AMD
                snprintf(gpuCardPath_, sizeof(gpuCardPath_), "/sys/class/drm/card%d/device", i);
                gpuVendor_ = 1;
                char hwmonBase[256];
                snprintf(hwmonBase, sizeof(hwmonBase), "%s/hwmon", gpuCardPath_);
                DIR* dir = opendir(hwmonBase);
                if (dir) {
                    struct dirent* ent = nullptr;
                    while ((ent = readdir(dir)) != nullptr) {
                        if (strncmp(ent->d_name, "hwmon", 5) == 0) {
                            snprintf(gpuHwmonPath_, sizeof(gpuHwmonPath_), "%s/%s", hwmonBase, ent->d_name);
                            break;
                        }
                    }
                    closedir(dir);
                }
                break;
            } else if (strcmp(vendor, "0x8086") == 0) { // Intel
                snprintf(gpuCardPath_, sizeof(gpuCardPath_), "/sys/class/drm/card%d/device", i);
                gpuVendor_ = 2;
                char hwmonBase[256];
                snprintf(hwmonBase, sizeof(hwmonBase), "%s/hwmon", gpuCardPath_);
                DIR* dir = opendir(hwmonBase);
                if (dir) {
                    struct dirent* ent = nullptr;
                    while ((ent = readdir(dir)) != nullptr) {
                        if (strncmp(ent->d_name, "hwmon", 5) == 0) {
                            snprintf(gpuHwmonPath_, sizeof(gpuHwmonPath_), "%s/%s", hwmonBase, ent->d_name);
                            break;
                        }
                    }
                    closedir(dir);
                }
                break;
            }
        }
    }

    void EnsureCpuPaths() {
        if (cpuPathsInitialized_) return;
        cpuPathsInitialized_ = true;

        // CPU Name from /proc/cpuinfo
        int fd = open("/proc/cpuinfo", O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            char buf[2048]{};
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n > 0) {
                buf[n] = '\0';
                char* pos = strstr(buf, "model name");
                if (pos) {
                    char* col = strchr(pos, ':');
                    if (col) {
                        col++;
                        while (*col == ' ' || *col == '\t') col++;
                        char* eol = strchr(col, '\n');
                        if (eol) *eol = '\0';
                        snprintf(cpuName_, sizeof(cpuName_), "%s", col);
                    }
                }
            }
        }
        if (cpuName_[0] == '\0') {
            snprintf(cpuName_, sizeof(cpuName_), "Linux CPU");
        }

        // CPU RAPL Powercap
        const char* raplPath = "/sys/class/powercap/intel-rapl/intel-rapl:0/energy_uj";
        if (access(raplPath, R_OK) == 0) {
            snprintf(cpuRaplPath_, sizeof(cpuRaplPath_), "%s", raplPath);
            prevEnergyUj_ = ReadSysfsUint64(cpuRaplPath_);
            prevEnergyTime_ = std::chrono::steady_clock::now();
        }

        // CPU Temperature Hwmon
        DIR* dir = opendir("/sys/class/hwmon");
        if (dir) {
            struct dirent* ent = nullptr;
            while ((ent = readdir(dir)) != nullptr) {
                if (ent->d_name[0] == '.') continue;
                char namePath[256];
                snprintf(namePath, sizeof(namePath), "/sys/class/hwmon/%s/name", ent->d_name);
                char name[32]{};
                if (ReadSysfsString(namePath, name, sizeof(name))) {
                    if (strcmp(name, "k10temp") == 0 || strcmp(name, "coretemp") == 0 ||
                        strcmp(name, "zenpower") == 0 || strcmp(name, "cpu_thermal") == 0) {
                        snprintf(cpuHwmonPath_, sizeof(cpuHwmonPath_), "/sys/class/hwmon/%s", ent->d_name);
                        break;
                    }
                }
            }
            closedir(dir);
        }
    }

    void SampleGpu(ipc::TelemetrySnapshot& snap) {
        EnsureGpuPaths();

        if (gpuVendor_ == 1 && gpuCardPath_[0] != '\0') { // AMD
            char path[512];
            snprintf(path, sizeof(path), "%s/gpu_busy_percent", gpuCardPath_);
            snap.gpuUtil = static_cast<float>(ReadSysfsDouble(path));

            snprintf(path, sizeof(path), "%s/mem_info_vram_used", gpuCardPath_);
            uint64_t vramUsed = ReadSysfsUint64(path);
            snprintf(path, sizeof(path), "%s/mem_info_vram_total", gpuCardPath_);
            uint64_t vramTotal = ReadSysfsUint64(path);
            snap.vramUsedGb = static_cast<float>(static_cast<double>(vramUsed) / (1024.0 * 1024.0 * 1024.0));
            snap.vramTotalGb = static_cast<float>(static_cast<double>(vramTotal) / (1024.0 * 1024.0 * 1024.0));

            if (gpuHwmonPath_[0] != '\0') {
                snprintf(path, sizeof(path), "%s/temp1_input", gpuHwmonPath_);
                snap.gpuTemp = static_cast<float>(ReadSysfsDouble(path, 1000.0));
                snprintf(path, sizeof(path), "%s/power1_average", gpuHwmonPath_);
                double power = ReadSysfsDouble(path, 1'000'000.0);
                if (power <= 0.0) {
                    snprintf(path, sizeof(path), "%s/power1_input", gpuHwmonPath_);
                    power = ReadSysfsDouble(path, 1'000'000.0);
                }
                snap.gpuPower = static_cast<float>(power);
                snprintf(path, sizeof(path), "%s/freq1_input", gpuHwmonPath_);
                snap.gpuFreq = static_cast<float>(ReadSysfsDouble(path, 1'000'000.0));
                snprintf(path, sizeof(path), "%s/fan1_input", gpuHwmonPath_);
                snap.gpuFanSpeed = static_cast<float>(ReadSysfsDouble(path));
                snprintf(path, sizeof(path), "%s/in0_input", gpuHwmonPath_);
                snap.gpuVoltage = static_cast<float>(ReadSysfsDouble(path));
            }
        } else if (gpuVendor_ == 2 && gpuCardPath_[0] != '\0') { // Intel
            if (gpuHwmonPath_[0] != '\0') {
                char path[512];
                snprintf(path, sizeof(path), "%s/temp1_input", gpuHwmonPath_);
                snap.gpuTemp = static_cast<float>(ReadSysfsDouble(path, 1000.0));
                snprintf(path, sizeof(path), "%s/power1_average", gpuHwmonPath_);
                snap.gpuPower = static_cast<float>(ReadSysfsDouble(path, 1'000'000.0));
                snprintf(path, sizeof(path), "%s/freq1_input", gpuHwmonPath_);
                snap.gpuFreq = static_cast<float>(ReadSysfsDouble(path, 1'000'000.0));
            }
        }
    }

    void SampleCpu(ipc::TelemetrySnapshot& snap) {
        EnsureCpuPaths();

        if (snap.cpuName[0] == '\0' && cpuName_[0] != '\0') {
            snprintf(snap.cpuName, sizeof(snap.cpuName), "%s", cpuName_);
        }

        // CPU Utilization from /proc/stat
        int fd = open("/proc/stat", O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            char buf[512]{};
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n > 0) {
                buf[n] = '\0';
                uint64_t u = 0, ni = 0, s = 0, id = 0, io = 0, ir = 0, sir = 0, st = 0;
                if (sscanf(buf, "cpu %lu %lu %lu %lu %lu %lu %lu %lu",
                           &u, &ni, &s, &id, &io, &ir, &sir, &st) >= 4) {
                    uint64_t total = u + ni + s + id + io + ir + sir + st;
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
        }

        // CPU Temp
        if (cpuHwmonPath_[0] != '\0') {
            char path[512];
            snprintf(path, sizeof(path), "%s/temp1_input", cpuHwmonPath_);
            double temp = ReadSysfsDouble(path, 1000.0);
            if (temp <= 0.0) {
                snprintf(path, sizeof(path), "%s/temp2_input", cpuHwmonPath_);
                temp = ReadSysfsDouble(path, 1000.0);
            }
            if (temp > 0.0) {
                snap.cpuTemp = static_cast<float>(temp);
            }
        }

        // CPU Power (RAPL)
        if (cpuRaplPath_[0] != '\0') {
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
        double khz = ReadSysfsDouble("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
        if (khz > 0.0) {
            snap.cpuFreq = static_cast<float>(khz / 1000.0);
        }
    }

    std::mutex mutex_;
    bool gpuPathsInitialized_ = false;
    int gpuVendor_ = 0; // 1 = AMD, 2 = Intel
    char gpuCardPath_[256]{};
    char gpuHwmonPath_[256]{};

    bool cpuPathsInitialized_ = false;
    char cpuName_[128]{};
    char cpuHwmonPath_[256]{};
    char cpuRaplPath_[256]{};
    uint64_t prevCpuTotal_ = 0;
    uint64_t prevCpuIdle_ = 0;
    uint64_t prevEnergyUj_ = 0;
    std::chrono::steady_clock::time_point prevEnergyTime_{};

    std::chrono::steady_clock::time_point lastSampleTime_{};
    ipc::TelemetrySnapshot cachedSnap_{};
};

} // namespace gnumon::layer
