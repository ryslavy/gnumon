#include "NvmlGpuTelemetry.h"
#include <iostream>
#include <cstring>

namespace gnumon::control {

NvmlGpuTelemetry::NvmlGpuTelemetry() = default;

NvmlGpuTelemetry::~NvmlGpuTelemetry() {
    if (isInitialized_ && nvmlShutdown_) {
        nvmlShutdown_();
    }
    if (libNvml_) {
        dlclose(libNvml_);
        libNvml_ = nullptr;
    }
}

bool NvmlGpuTelemetry::Initialize() {
    if (isInitialized_) return true;

    // Dynamically load libnvidia-ml.so
    libNvml_ = dlopen("libnvidia-ml.so.1", RTLD_NOW);
    if (!libNvml_) {
        libNvml_ = dlopen("libnvidia-ml.so", RTLD_NOW);
    }
    if (!libNvml_) {
        return false;
    }

    nvmlInit_ = reinterpret_cast<nvmlInit_f>(dlsym(libNvml_, "nvmlInit_v2"));
    if (!nvmlInit_) nvmlInit_ = reinterpret_cast<nvmlInit_f>(dlsym(libNvml_, "nvmlInit"));

    nvmlShutdown_ = reinterpret_cast<nvmlShutdown_f>(dlsym(libNvml_, "nvmlShutdown"));
    nvmlDeviceGetCount_ = reinterpret_cast<nvmlDeviceGetCount_f>(dlsym(libNvml_, "nvmlDeviceGetCount_v2"));
    if (!nvmlDeviceGetCount_) nvmlDeviceGetCount_ = reinterpret_cast<nvmlDeviceGetCount_f>(dlsym(libNvml_, "nvmlDeviceGetCount"));

    nvmlDeviceGetHandleByIndex_ = reinterpret_cast<nvmlDeviceGetHandleByIndex_f>(dlsym(libNvml_, "nvmlDeviceGetHandleByIndex_v2"));
    if (!nvmlDeviceGetHandleByIndex_) nvmlDeviceGetHandleByIndex_ = reinterpret_cast<nvmlDeviceGetHandleByIndex_f>(dlsym(libNvml_, "nvmlDeviceGetHandleByIndex"));

    nvmlDeviceGetName_ = reinterpret_cast<nvmlDeviceGetName_f>(dlsym(libNvml_, "nvmlDeviceGetName"));
    nvmlDeviceGetUtilizationRates_ = reinterpret_cast<nvmlDeviceGetUtilizationRates_f>(dlsym(libNvml_, "nvmlDeviceGetUtilizationRates"));
    nvmlDeviceGetTemperature_ = reinterpret_cast<nvmlDeviceGetTemperature_f>(dlsym(libNvml_, "nvmlDeviceGetTemperature"));
    nvmlDeviceGetPowerUsage_ = reinterpret_cast<nvmlDeviceGetPowerUsage_f>(dlsym(libNvml_, "nvmlDeviceGetPowerUsage"));
    nvmlDeviceGetClockInfo_ = reinterpret_cast<nvmlDeviceGetClockInfo_f>(dlsym(libNvml_, "nvmlDeviceGetClockInfo"));
    nvmlDeviceGetMemoryInfo_ = reinterpret_cast<nvmlDeviceGetMemoryInfo_f>(dlsym(libNvml_, "nvmlDeviceGetMemoryInfo"));
    nvmlDeviceGetFanSpeed_ = reinterpret_cast<nvmlDeviceGetFanSpeed_f>(dlsym(libNvml_, "nvmlDeviceGetFanSpeed"));

    if (!nvmlInit_ || nvmlInit_() != 0) {
        dlclose(libNvml_);
        libNvml_ = nullptr;
        return false;
    }

    unsigned int count = 0;
    if (nvmlDeviceGetCount_ && nvmlDeviceGetCount_(&count) == 0 && count > 0) {
        if (nvmlDeviceGetHandleByIndex_ && nvmlDeviceGetHandleByIndex_(0, &deviceHandle_) == 0) {
            isInitialized_ = true;
            return true;
        }
    }

    return false;
}

bool NvmlGpuTelemetry::Sample(GpuMetrics& metrics) {
    if (!isInitialized_ && !Initialize()) {
        return false;
    }

    metrics.vendor = PM_DEVICE_VENDOR_NVIDIA;

    char nameBuf[96]{};
    if (nvmlDeviceGetName_ && nvmlDeviceGetName_(deviceHandle_, nameBuf, sizeof(nameBuf)) == 0) {
        metrics.deviceName = nameBuf;
    } else {
        metrics.deviceName = "NVIDIA GeForce GPU";
    }

    if (nvmlDeviceGetUtilizationRates_) {
        nvmlUtilization_t util{};
        if (nvmlDeviceGetUtilizationRates_(deviceHandle_, &util) == 0) {
            metrics.gpuUtilizationPercent = static_cast<double>(util.gpu);
            metrics.memUtilizationPercent = static_cast<double>(util.memory);
        }
    }

    if (nvmlDeviceGetTemperature_) {
        unsigned int temp = 0;
        if (nvmlDeviceGetTemperature_(deviceHandle_, 0 /* NVML_TEMPERATURE_GPU */, &temp) == 0) {
            metrics.temperatureEdgeC = static_cast<double>(temp);
        }
    }

    if (nvmlDeviceGetPowerUsage_) {
        unsigned int mw = 0;
        if (nvmlDeviceGetPowerUsage_(deviceHandle_, &mw) == 0) {
            metrics.powerWatts = static_cast<double>(mw) / 1000.0;
        }
    }

    if (nvmlDeviceGetClockInfo_) {
        unsigned int mhz = 0;
        if (nvmlDeviceGetClockInfo_(deviceHandle_, 0 /* NVML_CLOCK_GRAPHICS */, &mhz) == 0) {
            metrics.gpuFrequencyMhz = static_cast<double>(mhz);
        }
        if (nvmlDeviceGetClockInfo_(deviceHandle_, 2 /* NVML_CLOCK_MEM */, &mhz) == 0) {
            metrics.memFrequencyMhz = static_cast<double>(mhz);
        }
    }

    if (nvmlDeviceGetMemoryInfo_) {
        nvmlMemory_t mem{};
        if (nvmlDeviceGetMemoryInfo_(deviceHandle_, &mem) == 0) {
            metrics.vramUsedBytes = mem.used;
            metrics.vramTotalBytes = mem.total;
        }
    }

    if (nvmlDeviceGetFanSpeed_) {
        unsigned int speed = 0;
        if (nvmlDeviceGetFanSpeed_(deviceHandle_, &speed) == 0) {
            metrics.fanSpeedRpm = static_cast<double>(speed);
        }
    }

    return true;
}

} // namespace gnumon::control
