#pragma once

#include "../../include/gnumon/PresentMonAPI.h"
#include "../service/TelemetryCoordinator.h"
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>

namespace gnumon::api {

class LinearArena {
public:
    explicit LinearArena(size_t capacity = 1024 * 1024) : capacity_(capacity) {
        buffer_ = static_cast<uint8_t*>(std::malloc(capacity));
    }

    ~LinearArena() = default;

    void* Allocate(size_t bytes, size_t alignment = alignof(std::max_align_t)) {
        size_t padding = (alignment - (offset_ % alignment)) % alignment;
        if (offset_ + padding + bytes > capacity_) {
            return nullptr;
        }
        offset_ += padding;
        void* ptr = buffer_ + offset_;
        offset_ += bytes;
        return ptr;
    }

    uint8_t* GetBase() const { return buffer_; }

    PM_INTROSPECTION_STRING* MakeString(const char* str) {
        if (!str) str = "";
        auto* s = static_cast<PM_INTROSPECTION_STRING*>(Allocate(sizeof(PM_INTROSPECTION_STRING)));
        size_t len = std::strlen(str) + 1;
        char* data = static_cast<char*>(Allocate(len, 1));
        std::memcpy(data, str, len);
        s->pData = data;
        return s;
    }

    template<typename T>
    PM_INTROSPECTION_OBJARRAY* MakeObjArray(const std::vector<T*>& items) {
        auto* arr = static_cast<PM_INTROSPECTION_OBJARRAY*>(Allocate(sizeof(PM_INTROSPECTION_OBJARRAY)));
        arr->size = items.size();
        if (items.empty()) {
            arr->pData = nullptr;
            return arr;
        }
        auto* data = static_cast<const void**>(Allocate(sizeof(void*) * items.size()));
        for (size_t i = 0; i < items.size(); ++i) {
            data[i] = items[i];
        }
        arr->pData = data;
        return arr;
    }

private:
    uint8_t* buffer_ = nullptr;
    size_t capacity_ = 0;
    size_t offset_ = 0;
};

inline const PM_INTROSPECTION_ROOT* BuildIntrospectionTree(service::TelemetryCoordinator* coordinator) {
    LinearArena arena(512 * 1024);

    // Allocate root at offset 0 so free(root) frees the entire arena block
    auto* root = static_cast<PM_INTROSPECTION_ROOT*>(arena.Allocate(sizeof(PM_INTROSPECTION_ROOT)));
    if (!root) return nullptr;

    // 1. Devices
    std::vector<PM_INTROSPECTION_DEVICE*> devices;

    // Device 0: System
    auto* devSys = static_cast<PM_INTROSPECTION_DEVICE*>(arena.Allocate(sizeof(PM_INTROSPECTION_DEVICE)));
    devSys->id = 0;
    devSys->type = PM_DEVICE_TYPE_SYSTEM;
    devSys->vendor = PM_DEVICE_VENDOR_UNKNOWN;
    devSys->pName = arena.MakeString("System Host CPU");
    devSys->pLuid = nullptr;
    devices.push_back(devSys);

    // Device 1: GPU
    std::string gpuName = "Graphics Adapter";
    PM_DEVICE_VENDOR gpuVendor = PM_DEVICE_VENDOR_UNKNOWN;
    if (coordinator) {
        auto gpu = coordinator->GetLatestGpuMetrics();
        if (!gpu.deviceName.empty()) gpuName = gpu.deviceName;
        gpuVendor = static_cast<PM_DEVICE_VENDOR>(gpu.vendor);
    }
    auto* devGpu = static_cast<PM_INTROSPECTION_DEVICE*>(arena.Allocate(sizeof(PM_INTROSPECTION_DEVICE)));
    devGpu->id = 1;
    devGpu->type = PM_DEVICE_TYPE_GRAPHICS_ADAPTER;
    devGpu->vendor = gpuVendor;
    devGpu->pName = arena.MakeString(gpuName.c_str());
    devGpu->pLuid = nullptr;
    devices.push_back(devGpu);

    root->pDevices = arena.MakeObjArray(devices);

    // 2. Units
    std::vector<PM_INTROSPECTION_UNIT*> units;
    auto addUnit = [&](PM_UNIT id, PM_UNIT base, double scale) {
        auto* u = static_cast<PM_INTROSPECTION_UNIT*>(arena.Allocate(sizeof(PM_INTROSPECTION_UNIT)));
        u->id = id;
        u->baseUnitId = base;
        u->scale = scale;
        units.push_back(u);
    };
    addUnit(PM_UNIT_DIMENSIONLESS, PM_UNIT_DIMENSIONLESS, 1.0);
    addUnit(PM_UNIT_PERCENT, PM_UNIT_PERCENT, 1.0);
    addUnit(PM_UNIT_FPS, PM_UNIT_FPS, 1.0);
    addUnit(PM_UNIT_MILLISECONDS, PM_UNIT_MILLISECONDS, 1.0);
    addUnit(PM_UNIT_SECONDS, PM_UNIT_MILLISECONDS, 1000.0);
    addUnit(PM_UNIT_WATTS, PM_UNIT_WATTS, 1.0);
    addUnit(PM_UNIT_CELSIUS, PM_UNIT_CELSIUS, 1.0);
    addUnit(PM_UNIT_MEGAHERTZ, PM_UNIT_MEGAHERTZ, 1.0);
    addUnit(PM_UNIT_GIGAHERTZ, PM_UNIT_MEGAHERTZ, 1000.0);
    addUnit(PM_UNIT_RPM, PM_UNIT_RPM, 1.0);
    addUnit(PM_UNIT_MILLIVOLTS, PM_UNIT_MILLIVOLTS, 1.0);
    addUnit(PM_UNIT_VOLTS, PM_UNIT_MILLIVOLTS, 1000.0);
    addUnit(PM_UNIT_BYTES, PM_UNIT_BYTES, 1.0);
    addUnit(PM_UNIT_MEGABYTES, PM_UNIT_BYTES, 1024.0 * 1024.0);
    addUnit(PM_UNIT_GIGABYTES, PM_UNIT_BYTES, 1024.0 * 1024.0 * 1024.0);

    root->pUnits = arena.MakeObjArray(units);

    // Standard stats
    std::vector<PM_INTROSPECTION_STAT_INFO*> stats;
    auto addStat = [&](PM_STAT s) {
        auto* st = static_cast<PM_INTROSPECTION_STAT_INFO*>(arena.Allocate(sizeof(PM_INTROSPECTION_STAT_INFO)));
        st->stat = s;
        stats.push_back(st);
    };
    addStat(PM_STAT_AVG);
    addStat(PM_STAT_MIN);
    addStat(PM_STAT_MAX);
    addStat(PM_STAT_PERCENTILE_99);
    addStat(PM_STAT_PERCENTILE_95);
    addStat(PM_STAT_PERCENTILE_90);
    addStat(PM_STAT_PERCENTILE_01);
    addStat(PM_STAT_PERCENTILE_05);
    addStat(PM_STAT_PERCENTILE_10);
    auto* statsArray = arena.MakeObjArray(stats);

    // Standard device metric info
    std::vector<PM_INTROSPECTION_DEVICE_METRIC_INFO*> devMetricInfo;
    auto* dmi = static_cast<PM_INTROSPECTION_DEVICE_METRIC_INFO*>(arena.Allocate(sizeof(PM_INTROSPECTION_DEVICE_METRIC_INFO)));
    dmi->deviceId = 0;
    dmi->availability = PM_METRIC_AVAILABILITY_AVAILABLE;
    dmi->arraySize = 1;
    devMetricInfo.push_back(dmi);
    auto* devMetricArray = arena.MakeObjArray(devMetricInfo);

    // 3. Metrics
    std::vector<PM_INTROSPECTION_METRIC*> metrics;
    auto addMetric = [&](PM_METRIC id, PM_METRIC_TYPE type, PM_UNIT unit, PM_DATA_TYPE polledType, PM_DATA_TYPE frameType) {
        auto* m = static_cast<PM_INTROSPECTION_METRIC*>(arena.Allocate(sizeof(PM_INTROSPECTION_METRIC)));
        m->id = id;
        m->type = type;
        m->unit = unit;
        m->preferredUnitHint = unit;

        auto* ti = static_cast<PM_INTROSPECTION_DATA_TYPE_INFO*>(arena.Allocate(sizeof(PM_INTROSPECTION_DATA_TYPE_INFO)));
        ti->polledType = polledType;
        ti->frameType = frameType;
        ti->enumId = PM_ENUM_NULL_ENUM;
        m->pTypeInfo = ti;

        m->pStatInfo = statsArray;
        m->pDeviceMetricInfo = devMetricArray;
        metrics.push_back(m);
    };

    addMetric(PM_METRIC_DISPLAYED_FPS, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_FPS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE);
    addMetric(PM_METRIC_PRESENTED_FPS, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_FPS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE);
    addMetric(PM_METRIC_APPLICATION_FPS, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_FPS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE);
    addMetric(PM_METRIC_CPU_FRAME_TIME, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE);
    addMetric(PM_METRIC_DISPLAYED_FRAME_TIME, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE);
    addMetric(PM_METRIC_PRESENTED_FRAME_TIME, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE);
    addMetric(PM_METRIC_GPU_TIME, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE);
    addMetric(PM_METRIC_GPU_BUSY, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE);
    addMetric(PM_METRIC_GPU_WAIT, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE);
    addMetric(PM_METRIC_UNTIL_DISPLAYED, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE);
    addMetric(PM_METRIC_DISPLAY_LATENCY, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_MILLISECONDS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE);
    addMetric(PM_METRIC_DROPPED_FRAMES, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_DIMENSIONLESS, PM_DATA_TYPE_UINT32, PM_DATA_TYPE_UINT32);

    addMetric(PM_METRIC_GPU_POWER, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_WATTS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE);
    addMetric(PM_METRIC_GPU_TEMPERATURE, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_CELSIUS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE);
    addMetric(PM_METRIC_GPU_UTILIZATION, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_PERCENT, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE);
    addMetric(PM_METRIC_GPU_FREQUENCY, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_MEGAHERTZ, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE);
    addMetric(PM_METRIC_GPU_VOLTAGE, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_MILLIVOLTS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE);
    addMetric(PM_METRIC_GPU_FAN_SPEED, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_RPM, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE);
    addMetric(PM_METRIC_GPU_MEM_USED, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_BYTES, PM_DATA_TYPE_UINT64, PM_DATA_TYPE_UINT64);
    addMetric(PM_METRIC_GPU_MEM_SIZE, PM_METRIC_TYPE_STATIC, PM_UNIT_BYTES, PM_DATA_TYPE_UINT64, PM_DATA_TYPE_UINT64);

    addMetric(PM_METRIC_CPU_UTILIZATION, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_PERCENT, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE);
    addMetric(PM_METRIC_CPU_POWER, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_WATTS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE);
    addMetric(PM_METRIC_CPU_TEMPERATURE, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_CELSIUS, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE);
    addMetric(PM_METRIC_CPU_FREQUENCY, PM_METRIC_TYPE_DYNAMIC, PM_UNIT_MEGAHERTZ, PM_DATA_TYPE_DOUBLE, PM_DATA_TYPE_DOUBLE);
    addMetric(PM_METRIC_CPU_NAME, PM_METRIC_TYPE_STATIC, PM_UNIT_DIMENSIONLESS, PM_DATA_TYPE_STRING, PM_DATA_TYPE_STRING);
    addMetric(PM_METRIC_GPU_NAME, PM_METRIC_TYPE_STATIC, PM_UNIT_DIMENSIONLESS, PM_DATA_TYPE_STRING, PM_DATA_TYPE_STRING);

    root->pMetrics = arena.MakeObjArray(metrics);

    // 4. Enums
    std::vector<PM_INTROSPECTION_ENUM*> enums;
    auto addEnum = [&](PM_ENUM id, const char* symbol, const char* desc) {
        auto* e = static_cast<PM_INTROSPECTION_ENUM*>(arena.Allocate(sizeof(PM_INTROSPECTION_ENUM)));
        e->id = id;
        e->pSymbol = arena.MakeString(symbol);
        e->pDescription = arena.MakeString(desc);
        std::vector<PM_INTROSPECTION_ENUM_KEY*> emptyKeys;
        e->pKeys = arena.MakeObjArray(emptyKeys);
        enums.push_back(e);
    };
    addEnum(PM_ENUM_STATUS, "PM_STATUS", "Status return code");
    addEnum(PM_ENUM_METRIC, "PM_METRIC", "Metric identifier");
    addEnum(PM_ENUM_UNIT, "PM_UNIT", "Unit of measurement");
    addEnum(PM_ENUM_STAT, "PM_STAT", "Statistical operation");
    addEnum(PM_ENUM_GRAPHICS_RUNTIME, "PM_GRAPHICS_RUNTIME", "Underlying 3D API");
    addEnum(PM_ENUM_DEVICE_VENDOR, "PM_DEVICE_VENDOR", "Hardware vendor");

    root->pEnums = arena.MakeObjArray(enums);

    return root;
}

} // namespace gnumon::api
