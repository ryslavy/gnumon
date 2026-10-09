#pragma once

#include "GpuTelemetry.h"
#include <dlfcn.h>
#include <string>

namespace gnumon::control {

class NvmlGpuTelemetry : public IGpuTelemetryProvider {
public:
    NvmlGpuTelemetry();
    ~NvmlGpuTelemetry() override;

    bool Initialize() override;
    bool Sample(GpuMetrics& metrics) override;
    const std::string& GetName() const override { return providerName_; }

private:
    std::string providerName_ = "NVIDIA NVML";
    void* libNvml_ = nullptr;
    void* deviceHandle_ = nullptr;
    bool isInitialized_ = false;

    // NVML Types & Function pointers
    typedef int nvmlReturn_t;
    typedef void* nvmlDevice_t;
    struct nvmlUtilization_t {
        unsigned int gpu;
        unsigned int memory;
    };
    struct nvmlMemory_t {
        unsigned long long total;
        unsigned long long free;
        unsigned long long used;
    };

    typedef nvmlReturn_t (*nvmlInit_f)();
    typedef nvmlReturn_t (*nvmlShutdown_f)();
    typedef nvmlReturn_t (*nvmlDeviceGetCount_f)(unsigned int*);
    typedef nvmlReturn_t (*nvmlDeviceGetHandleByIndex_f)(unsigned int, nvmlDevice_t*);
    typedef nvmlReturn_t (*nvmlDeviceGetName_f)(nvmlDevice_t, char*, unsigned int);
    typedef nvmlReturn_t (*nvmlDeviceGetUtilizationRates_f)(nvmlDevice_t, nvmlUtilization_t*);
    typedef nvmlReturn_t (*nvmlDeviceGetTemperature_f)(nvmlDevice_t, int, unsigned int*);
    typedef nvmlReturn_t (*nvmlDeviceGetPowerUsage_f)(nvmlDevice_t, unsigned int*);
    typedef nvmlReturn_t (*nvmlDeviceGetClockInfo_f)(nvmlDevice_t, int, unsigned int*);
    typedef nvmlReturn_t (*nvmlDeviceGetMemoryInfo_f)(nvmlDevice_t, nvmlMemory_t*);
    typedef nvmlReturn_t (*nvmlDeviceGetFanSpeed_f)(nvmlDevice_t, unsigned int*);
    typedef nvmlReturn_t (*nvmlDeviceGetPowerManagementLimit_f)(nvmlDevice_t, unsigned int*);

    nvmlInit_f nvmlInit_ = nullptr;
    nvmlShutdown_f nvmlShutdown_ = nullptr;
    nvmlDeviceGetCount_f nvmlDeviceGetCount_ = nullptr;
    nvmlDeviceGetHandleByIndex_f nvmlDeviceGetHandleByIndex_ = nullptr;
    nvmlDeviceGetName_f nvmlDeviceGetName_ = nullptr;
    nvmlDeviceGetUtilizationRates_f nvmlDeviceGetUtilizationRates_ = nullptr;
    nvmlDeviceGetTemperature_f nvmlDeviceGetTemperature_ = nullptr;
    nvmlDeviceGetPowerUsage_f nvmlDeviceGetPowerUsage_ = nullptr;
    nvmlDeviceGetClockInfo_f nvmlDeviceGetClockInfo_ = nullptr;
    nvmlDeviceGetMemoryInfo_f nvmlDeviceGetMemoryInfo_ = nullptr;
    nvmlDeviceGetFanSpeed_f nvmlDeviceGetFanSpeed_ = nullptr;
    nvmlDeviceGetPowerManagementLimit_f nvmlDeviceGetPowerManagementLimit_ = nullptr;
};

} // namespace gnumon::control
