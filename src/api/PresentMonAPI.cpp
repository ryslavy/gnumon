#include "../../include/gnumon/PresentMonAPI.h"
#include "../service/TelemetryCoordinator.h"
#include "../common/Clock.h"
#include "IntrospectionBuilder.h"
#include <cstring>
#include <memory>
#include <vector>
#include <iostream>

struct PM_SESSION {
    std::unique_ptr<gnumon::service::TelemetryCoordinator> coordinator;
    bool active = false;
};

struct PM_DYNAMIC_QUERY {
    PM_SESSION* session = nullptr;
    std::vector<PM_QUERY_ELEMENT> elements;
    double windowSizeMs = 1000.0;
    double metricOffsetMs = 0.0;
};

struct PM_FRAME_QUERY {
    PM_SESSION* session = nullptr;
    std::vector<PM_QUERY_ELEMENT> elements;
    uint32_t blobSize = 0;
};

extern "C" {

PRESENTMON_API2_EXPORT PM_STATUS pmGetApiVersion(PM_VERSION* pVersion) {
    if (!pVersion) return PM_STATUS_BAD_ARGUMENT;
    pVersion->major = PM_API_VERSION_MAJOR;
    pVersion->minor = PM_API_VERSION_MINOR;
    pVersion->patch = 0;
    std::snprintf(pVersion->tag, sizeof(pVersion->tag), "gnumon-linux-0.1");
    std::snprintf(pVersion->hash, sizeof(pVersion->hash), "git");
    std::snprintf(pVersion->config, sizeof(pVersion->config), "rel");
    return PM_STATUS_SUCCESS;
}

PRESENTMON_API2_EXPORT PM_STATUS pmOpenSession(PM_SESSION_HANDLE* pHandle) {
    if (!pHandle) return PM_STATUS_BAD_ARGUMENT;

    auto session = new PM_SESSION();
    session->coordinator = std::make_unique<gnumon::service::TelemetryCoordinator>();
    session->coordinator->Initialize();
    session->active = true;

    *pHandle = session;
    return PM_STATUS_SUCCESS;
}

PRESENTMON_API2_EXPORT PM_STATUS pmOpenSessionWithPipe(PM_SESSION_HANDLE* pHandle, const char* /*controlPipeName*/) {
    return pmOpenSession(pHandle);
}

PRESENTMON_API2_EXPORT PM_STATUS pmCloseSession(PM_SESSION_HANDLE handle) {
    if (!handle) return PM_STATUS_BAD_HANDLE;
    delete handle;
    return PM_STATUS_SUCCESS;
}

PRESENTMON_API2_EXPORT PM_STATUS pmStartTrackingProcess(PM_SESSION_HANDLE handle, uint32_t process_id) {
    if (!handle || !handle->active) return PM_STATUS_BAD_HANDLE;
    if (handle->coordinator) {
        handle->coordinator->StartTrackingProcess(process_id);
    }
    return PM_STATUS_SUCCESS;
}

PRESENTMON_API2_EXPORT PM_STATUS pmStopTrackingProcess(PM_SESSION_HANDLE handle, uint32_t process_id) {
    if (!handle || !handle->active) return PM_STATUS_BAD_HANDLE;
    if (handle->coordinator) {
        handle->coordinator->StopTrackingProcess(process_id);
    }
    return PM_STATUS_SUCCESS;
}

PRESENTMON_API2_EXPORT PM_STATUS pmGetIntrospectionRoot(PM_SESSION_HANDLE handle, const PM_INTROSPECTION_ROOT** ppRoot) {
    if (!handle || !ppRoot) return PM_STATUS_BAD_ARGUMENT;
    *ppRoot = gnumon::api::BuildIntrospectionTree(handle->coordinator.get());
    return *ppRoot ? PM_STATUS_SUCCESS : PM_STATUS_FAILURE;
}

PRESENTMON_API2_EXPORT PM_STATUS pmFreeIntrospectionRoot(const PM_INTROSPECTION_ROOT* pRoot) {
    if (!pRoot) return PM_STATUS_SUCCESS;
    std::free(const_cast<PM_INTROSPECTION_ROOT*>(pRoot));
    return PM_STATUS_SUCCESS;
}

PRESENTMON_API2_EXPORT PM_STATUS pmRegisterDynamicQuery(
    PM_SESSION_HANDLE sessionHandle,
    PM_DYNAMIC_QUERY_HANDLE* pHandle,
    PM_QUERY_ELEMENT* pElements,
    uint64_t numElements,
    double windowSizeMs,
    double metricOffsetMs)
{
    if (!sessionHandle || !pHandle || !pElements || numElements == 0) {
        return PM_STATUS_BAD_ARGUMENT;
    }

    auto query = new PM_DYNAMIC_QUERY();
    query->session = sessionHandle;
    query->windowSizeMs = windowSizeMs;
    query->metricOffsetMs = metricOffsetMs;
    query->elements.assign(pElements, pElements + numElements);

    *pHandle = query;
    return PM_STATUS_SUCCESS;
}

PRESENTMON_API2_EXPORT PM_STATUS pmFreeDynamicQuery(PM_DYNAMIC_QUERY_HANDLE handle) {
    if (!handle) return PM_STATUS_BAD_HANDLE;
    delete handle;
    return PM_STATUS_SUCCESS;
}

PRESENTMON_API2_EXPORT PM_STATUS pmPollDynamicQuery(
    PM_DYNAMIC_QUERY_HANDLE handle,
    uint32_t /*processId*/,
    uint8_t* pBlob,
    uint32_t* numSwapChains)
{
    if (!handle || !pBlob) return PM_STATUS_BAD_ARGUMENT;
    if (numSwapChains) *numSwapChains = 1;

    // Trigger sampling in coordinator
    auto* coordinator = handle->session->coordinator.get();
    if (coordinator) {
        coordinator->SampleAll();
        auto gpu = coordinator->GetLatestGpuMetrics();
        auto cpu = coordinator->GetLatestCpuMetrics();

        gnumon::ipc::FrameEvent frame{};
        bool hasFrame = coordinator->GetLatestFrame(frame);

        for (const auto& elem : handle->elements) {
            uint8_t* dest = pBlob + elem.dataOffset;
            switch (elem.metric) {
            // Hardware metrics
            case PM_METRIC_GPU_POWER:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = gpu.powerWatts;
                }
                break;
            case PM_METRIC_GPU_TEMPERATURE:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = gpu.temperatureEdgeC;
                }
                break;
            case PM_METRIC_GPU_UTILIZATION:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = gpu.gpuUtilizationPercent;
                }
                break;
            case PM_METRIC_GPU_FREQUENCY:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = gpu.gpuFrequencyMhz;
                }
                break;
            case PM_METRIC_CPU_UTILIZATION:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = cpu.cpuUtilizationPercent;
                }
                break;
            case PM_METRIC_CPU_POWER:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = cpu.cpuPackagePowerWatts;
                }
                break;
            case PM_METRIC_CPU_TEMPERATURE:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = cpu.cpuTemperatureC;
                }
                break;
            case PM_METRIC_CPU_FREQUENCY:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = cpu.cpuFrequencyMhz;
                }
                break;
            case PM_METRIC_CPU_CORE_UTILITY:
                if (elem.dataSize >= sizeof(double)) {
                    double util = 0.0;
                    if (elem.arrayIndex < cpu.perCoreUtilization.size()) {
                        util = cpu.perCoreUtilization[elem.arrayIndex];
                    }
                    *reinterpret_cast<double*>(dest) = util;
                }
                break;
            case PM_METRIC_CPU_CORE_TEMPERATURE:
                if (elem.dataSize >= sizeof(double)) {
                    double temp = cpu.cpuTemperatureC;
                    if (elem.arrayIndex < cpu.perCoreTemperature.size()) {
                        temp = cpu.perCoreTemperature[elem.arrayIndex];
                    }
                    *reinterpret_cast<double*>(dest) = temp;
                }
                break;
            case PM_METRIC_CPU_NAME:
                if (elem.dataSize > 0) {
                    std::strncpy(reinterpret_cast<char*>(dest), cpu.cpuName.c_str(), elem.dataSize - 1);
                    dest[elem.dataSize - 1] = '\0';
                }
                break;
            case PM_METRIC_CPU_VENDOR:
                if (elem.dataSize >= sizeof(int32_t)) {
                    *reinterpret_cast<int32_t*>(dest) = cpu.cpuVendor;
                }
                break;
            case PM_METRIC_GPU_NAME:
                if (elem.dataSize > 0) {
                    std::strncpy(reinterpret_cast<char*>(dest), gpu.deviceName.c_str(), elem.dataSize - 1);
                    dest[elem.dataSize - 1] = '\0';
                }
                break;
            case PM_METRIC_GPU_VENDOR:
                if (elem.dataSize >= sizeof(int32_t)) {
                    *reinterpret_cast<int32_t*>(dest) = gpu.vendor;
                }
                break;
            case PM_METRIC_GPU_MEM_USED:
                if (elem.dataSize >= sizeof(uint64_t)) {
                    *reinterpret_cast<uint64_t*>(dest) = gpu.vramUsedBytes;
                }
                break;
            case PM_METRIC_GPU_MEM_SIZE:
                if (elem.dataSize >= sizeof(uint64_t)) {
                    *reinterpret_cast<uint64_t*>(dest) = gpu.vramTotalBytes;
                }
                break;
            case PM_METRIC_GPU_FAN_SPEED:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = gpu.fanSpeedRpm;
                }
                break;

            // Frame presentation metrics (from Vulkan Layer)
            case PM_METRIC_DISPLAYED_FPS:
            case PM_METRIC_PRESENTED_FPS:
            case PM_METRIC_APPLICATION_FPS:
                if (elem.dataSize >= sizeof(double)) {
                    double fps = 0.0;
                    if (elem.stat != PM_STAT_NONE) {
                        fps = coordinator->GetStatisticalMetric(elem.metric, elem.stat, handle->windowSizeMs);
                    } else if (hasFrame && frame.frameTimeNs > 0) {
                        fps = 1'000'000'000.0 / static_cast<double>(frame.frameTimeNs);
                    }
                    *reinterpret_cast<double*>(dest) = fps;
                }
                break;
            case PM_METRIC_CPU_FRAME_TIME:
            case PM_METRIC_DISPLAYED_FRAME_TIME:
            case PM_METRIC_PRESENTED_FRAME_TIME:
                if (elem.dataSize >= sizeof(double)) {
                    double ftMs = 0.0;
                    if (elem.stat != PM_STAT_NONE) {
                        ftMs = coordinator->GetStatisticalMetric(elem.metric, elem.stat, handle->windowSizeMs);
                    } else if (hasFrame && frame.frameTimeNs > 0) {
                        ftMs = static_cast<double>(frame.frameTimeNs) / 1'000'000.0;
                    }
                    *reinterpret_cast<double*>(dest) = ftMs;
                }
                break;
            case PM_METRIC_IN_PRESENT_API:
                if (elem.dataSize >= sizeof(double)) {
                    double presentMs = hasFrame
                        ? (static_cast<double>(frame.presentDurationNs) / 1'000'000.0)
                        : 0.0;
                    *reinterpret_cast<double*>(dest) = presentMs;
                }
                break;
            case PM_METRIC_PRESENT_START_TIME:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = hasFrame
                        ? static_cast<double>(frame.presentStartTimestampNs)
                        : 0.0;
                }
                break;
            case PM_METRIC_PRESENT_START_QPC:
                if (elem.dataSize >= sizeof(uint64_t)) {
                    *reinterpret_cast<uint64_t*>(dest) = hasFrame
                        ? frame.presentStartTimestampNs
                        : 0;
                }
                break;
            case PM_METRIC_SWAP_CHAIN_ADDRESS:
                if (elem.dataSize >= sizeof(uint64_t)) {
                    *reinterpret_cast<uint64_t*>(dest) = hasFrame
                        ? frame.swapChain
                        : 0;
                }
                break;
            case PM_METRIC_PRESENT_RUNTIME:
                if (elem.dataSize >= sizeof(int32_t)) {
                    *reinterpret_cast<int32_t*>(dest) = PM_GRAPHICS_RUNTIME_VULKAN;
                }
                break;
            case PM_METRIC_FRAME_TYPE:
                if (elem.dataSize >= sizeof(int32_t)) {
                    *reinterpret_cast<int32_t*>(dest) = hasFrame ? static_cast<int32_t>(frame.frameType) : PM_FRAME_TYPE_APPLICATION;
                }
                break;
            case PM_METRIC_GPU_TIME:
                if (elem.dataSize >= sizeof(double)) {
                    double gpuTimeMs = hasFrame ? (static_cast<double>(frame.gpuDurationNs) / 1'000'000.0) : 0.0;
                    *reinterpret_cast<double*>(dest) = gpuTimeMs;
                }
                break;
            case PM_METRIC_GPU_BUSY:
                if (elem.dataSize >= sizeof(double)) {
                    double gpuBusyMs = hasFrame ? (static_cast<double>(frame.gpuBusyNs) / 1'000'000.0) : 0.0;
                    *reinterpret_cast<double*>(dest) = gpuBusyMs;
                }
                break;
            case PM_METRIC_GPU_WAIT:
                if (elem.dataSize >= sizeof(double)) {
                    double gpuWaitMs = hasFrame ? (static_cast<double>(frame.gpuWaitNs) / 1'000'000.0) : 0.0;
                    *reinterpret_cast<double*>(dest) = gpuWaitMs;
                }
                break;
            case PM_METRIC_UNTIL_DISPLAYED:
                if (elem.dataSize >= sizeof(double)) {
                    double untilDispMs = (hasFrame && frame.displayTimestampNs >= frame.presentStartTimestampNs)
                        ? (static_cast<double>(frame.displayTimestampNs - frame.presentStartTimestampNs) / 1'000'000.0)
                        : 0.0;
                    *reinterpret_cast<double*>(dest) = untilDispMs;
                }
                break;
            case PM_METRIC_DISPLAY_LATENCY:
            case PM_METRIC_PC_LATENCY:
            case PM_METRIC_RENDER_PRESENT_LATENCY:
                if (elem.dataSize >= sizeof(double)) {
                    double dispLatMs = 0.0;
                    if (elem.stat != PM_STAT_NONE) {
                        dispLatMs = coordinator ? coordinator->GetStatisticalMetric(elem.metric, elem.stat, handle->windowSizeMs) : 0.0;
                    } else if (hasFrame && frame.displayTimestampNs >= frame.cpuStartTimestampNs && frame.cpuStartTimestampNs > 0) {
                        dispLatMs = static_cast<double>(frame.displayTimestampNs - frame.cpuStartTimestampNs) / 1'000'000.0;
                    } else if (hasFrame && frame.gpuDurationNs > 0) {
                        dispLatMs = static_cast<double>(frame.gpuDurationNs + frame.presentDurationNs) / 1'000'000.0;
                    }
                    *reinterpret_cast<double*>(dest) = dispLatMs;
                }
                break;
            case PM_METRIC_ANIMATION_ERROR:
                if (elem.dataSize >= sizeof(double)) {
                    double animErr = 0.0;
                    if (elem.stat != PM_STAT_NONE) {
                        animErr = coordinator ? coordinator->GetStatisticalMetric(elem.metric, elem.stat, handle->windowSizeMs) : 0.0;
                    } else {
                        animErr = coordinator ? coordinator->GetStatisticalMetric(elem.metric, PM_STAT_AVG, handle->windowSizeMs) : 0.0;
                    }
                    *reinterpret_cast<double*>(dest) = animErr;
                }
                break;
            case PM_METRIC_CLICK_TO_PHOTON_LATENCY:
                if (elem.dataSize >= sizeof(double)) {
                    uint64_t lastClick = coordinator ? coordinator->GetLastClickTimestampNs() : 0;
                    double latencyMs = (hasFrame && lastClick > 0 && frame.presentStartTimestampNs > lastClick)
                        ? (static_cast<double>(frame.presentStartTimestampNs - lastClick) / 1'000'000.0)
                        : 0.0;
                    *reinterpret_cast<double*>(dest) = latencyMs;
                }
                break;
            case PM_METRIC_DROPPED_FRAMES:
                if (elem.dataSize >= sizeof(uint32_t)) {
                    *reinterpret_cast<uint32_t*>(dest) = hasFrame ? frame.dropped : 0;
                }
                break;
            case PM_METRIC_GPU_VOLTAGE:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = gpu.voltageMv;
                }
                break;
            case PM_METRIC_GPU_MEM_FREQUENCY:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = gpu.memFrequencyMhz;
                }
                break;
            case PM_METRIC_GPU_MEM_UTILIZATION:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = gpu.memUtilizationPercent;
                }
                break;
            case PM_METRIC_GPU_TEMPERATURE_LIMITED:
                if (elem.dataSize >= sizeof(uint32_t)) {
                    *reinterpret_cast<uint32_t*>(dest) = (gpu.temperatureEdgeC > 95.0 || gpu.temperatureHotspotC > 105.0) ? 1 : 0;
                }
                break;
            case PM_METRIC_GPU_POWER_LIMITED:
                if (elem.dataSize >= sizeof(uint32_t)) {
                    *reinterpret_cast<uint32_t*>(dest) = 0;
                }
                break;
            default:
                break;
            }
        }
    }

    return PM_STATUS_SUCCESS;
}

PRESENTMON_API2_EXPORT PM_STATUS pmPollDynamicQueryWithTimestamp(
    PM_DYNAMIC_QUERY_HANDLE handle,
    uint32_t processId,
    uint8_t* pBlob,
    uint32_t* numSwapChains,
    uint64_t /*nowTimestamp*/)
{
    return pmPollDynamicQuery(handle, processId, pBlob, numSwapChains);
}

PRESENTMON_API2_EXPORT PM_STATUS pmPollStaticQuery(
    PM_SESSION_HANDLE sessionHandle,
    const PM_QUERY_ELEMENT* pElement,
    uint32_t /*processId*/,
    uint8_t* pBlob)
{
    if (!sessionHandle || !pElement || !pBlob) return PM_STATUS_BAD_ARGUMENT;
    auto* coordinator = sessionHandle->coordinator.get();
    if (!coordinator) return PM_STATUS_SERVICE_ERROR;

    coordinator->SampleAll();
    auto gpu = coordinator->GetLatestGpuMetrics();
    auto cpu = coordinator->GetLatestCpuMetrics();

    switch (pElement->metric) {
    case PM_METRIC_CPU_NAME:
        if (pElement->dataSize > 0) {
            std::strncpy(reinterpret_cast<char*>(pBlob), cpu.cpuName.c_str(), pElement->dataSize - 1);
            pBlob[pElement->dataSize - 1] = '\0';
        }
        break;
    case PM_METRIC_CPU_VENDOR:
        if (pElement->dataSize >= sizeof(int32_t)) {
            *reinterpret_cast<int32_t*>(pBlob) = cpu.cpuVendor;
        }
        break;
    case PM_METRIC_GPU_NAME:
        if (pElement->dataSize > 0) {
            std::strncpy(reinterpret_cast<char*>(pBlob), gpu.deviceName.c_str(), pElement->dataSize - 1);
            pBlob[pElement->dataSize - 1] = '\0';
        }
        break;
    case PM_METRIC_GPU_VENDOR:
        if (pElement->dataSize >= sizeof(int32_t)) {
            *reinterpret_cast<int32_t*>(pBlob) = gpu.vendor;
        }
        break;
    case PM_METRIC_GPU_MEM_SIZE:
        if (pElement->dataSize >= sizeof(uint64_t)) {
            *reinterpret_cast<uint64_t*>(pBlob) = gpu.vramTotalBytes;
        }
        break;
    default:
        break;
    }
    return PM_STATUS_SUCCESS;
}

PRESENTMON_API2_EXPORT PM_STATUS pmRegisterFrameQuery(
    PM_SESSION_HANDLE sessionHandle,
    PM_FRAME_QUERY_HANDLE* pHandle,
    PM_QUERY_ELEMENT* pElements,
    uint64_t numElements,
    uint32_t* pBlobSize)
{
    if (!sessionHandle || !pHandle || !pElements || numElements == 0 || !pBlobSize) {
        return PM_STATUS_BAD_ARGUMENT;
    }

    auto query = new PM_FRAME_QUERY();
    query->session = sessionHandle;
    query->elements.assign(pElements, pElements + numElements);

    uint32_t calculatedSize = 0;
    for (const auto& elem : query->elements) {
        uint32_t needed = static_cast<uint32_t>(elem.dataOffset + elem.dataSize);
        if (needed > calculatedSize) {
            calculatedSize = needed;
        }
    }

    query->blobSize = calculatedSize;
    *pBlobSize = calculatedSize;
    *pHandle = query;
    return PM_STATUS_SUCCESS;
}

PRESENTMON_API2_EXPORT PM_STATUS pmConsumeFrames(
    PM_FRAME_QUERY_HANDLE handle,
    uint32_t /*processId*/,
    uint8_t* pBlobs,
    uint32_t* pNumFramesToRead)
{
    if (!handle || !pBlobs || !pNumFramesToRead) return PM_STATUS_BAD_ARGUMENT;

    uint32_t maxFrames = *pNumFramesToRead;
    uint32_t framesRead = 0;
    auto* coordinator = handle->session->coordinator.get();
    if (!coordinator) {
        *pNumFramesToRead = 0;
        return PM_STATUS_SUCCESS;
    }

    // Refresh telemetry
    coordinator->SampleAll();
    auto gpu = coordinator->GetLatestGpuMetrics();
    auto cpu = coordinator->GetLatestCpuMetrics();

    while (framesRead < maxFrames) {
        gnumon::ipc::FrameEvent event{};
        if (!coordinator->PopFrame(event)) {
            break; // No more frames queued
        }

        uint8_t* currentBlob = pBlobs + (framesRead * handle->blobSize);

        for (const auto& elem : handle->elements) {
            uint8_t* dest = currentBlob + elem.dataOffset;
            switch (elem.metric) {
            case PM_METRIC_PROCESS_ID:
                if (elem.dataSize >= sizeof(uint32_t)) {
                    *reinterpret_cast<uint32_t*>(dest) = event.processId;
                }
                break;
            case PM_METRIC_SWAP_CHAIN_ADDRESS:
                if (elem.dataSize >= sizeof(uint64_t)) {
                    *reinterpret_cast<uint64_t*>(dest) = event.swapChain;
                }
                break;
            case PM_METRIC_PRESENT_RUNTIME:
                if (elem.dataSize >= sizeof(int32_t)) {
                    *reinterpret_cast<int32_t*>(dest) = PM_GRAPHICS_RUNTIME_VULKAN;
                }
                break;
            case PM_METRIC_PRESENT_MODE:
                if (elem.dataSize >= sizeof(int32_t)) {
                    *reinterpret_cast<int32_t*>(dest) = PM_PRESENT_MODE_COMPOSED_FLIP;
                }
                break;
            case PM_METRIC_FRAME_TYPE:
                if (elem.dataSize >= sizeof(int32_t)) {
                    *reinterpret_cast<int32_t*>(dest) = static_cast<int32_t>(event.frameType);
                }
                break;
            case PM_METRIC_CPU_START_TIME:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = static_cast<double>(event.cpuStartTimestampNs) / 1'000'000.0;
                }
                break;
            case PM_METRIC_CPU_START_QPC:
                if (elem.dataSize >= sizeof(uint64_t)) {
                    *reinterpret_cast<uint64_t*>(dest) = event.cpuStartTimestampNs;
                }
                break;
            case PM_METRIC_CPU_FRAME_TIME:
            case PM_METRIC_DISPLAYED_FRAME_TIME:
            case PM_METRIC_PRESENTED_FRAME_TIME:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = static_cast<double>(event.frameTimeNs) / 1'000'000.0;
                }
                break;
            case PM_METRIC_DISPLAYED_FPS:
            case PM_METRIC_PRESENTED_FPS:
            case PM_METRIC_APPLICATION_FPS:
                if (elem.dataSize >= sizeof(double)) {
                    double fps = (event.frameTimeNs > 0)
                        ? (1'000'000'000.0 / static_cast<double>(event.frameTimeNs))
                        : 0.0;
                    *reinterpret_cast<double*>(dest) = fps;
                }
                break;
            case PM_METRIC_IN_PRESENT_API:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = static_cast<double>(event.presentDurationNs) / 1'000'000.0;
                }
                break;
            case PM_METRIC_PRESENT_START_TIME:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = static_cast<double>(event.presentStartTimestampNs) / 1'000'000'000.0;
                }
                break;
            case PM_METRIC_PRESENT_START_QPC:
                if (elem.dataSize >= sizeof(uint64_t)) {
                    *reinterpret_cast<uint64_t*>(dest) = event.presentStartTimestampNs;
                }
                break;
            // Hardware metrics
            case PM_METRIC_GPU_POWER:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = gpu.powerWatts;
                }
                break;
            case PM_METRIC_GPU_TEMPERATURE:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = gpu.temperatureEdgeC;
                }
                break;
            case PM_METRIC_GPU_UTILIZATION:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = gpu.gpuUtilizationPercent;
                }
                break;
            case PM_METRIC_GPU_FREQUENCY:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = gpu.gpuFrequencyMhz;
                }
                break;
            case PM_METRIC_CPU_UTILIZATION:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = cpu.cpuUtilizationPercent;
                }
                break;
            case PM_METRIC_CPU_POWER:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = cpu.cpuPackagePowerWatts;
                }
                break;
            case PM_METRIC_CPU_TEMPERATURE:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = cpu.cpuTemperatureC;
                }
                break;
            case PM_METRIC_GPU_TIME:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = static_cast<double>(event.gpuDurationNs) / 1'000'000.0;
                }
                break;
            case PM_METRIC_GPU_BUSY:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = static_cast<double>(event.gpuBusyNs) / 1'000'000.0;
                }
                break;
            case PM_METRIC_GPU_WAIT:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = static_cast<double>(event.gpuWaitNs) / 1'000'000.0;
                }
                break;
            case PM_METRIC_UNTIL_DISPLAYED:
                if (elem.dataSize >= sizeof(double)) {
                    double untilDispMs = (event.displayTimestampNs >= event.presentStartTimestampNs)
                        ? (static_cast<double>(event.displayTimestampNs - event.presentStartTimestampNs) / 1'000'000.0)
                        : 0.0;
                    *reinterpret_cast<double*>(dest) = untilDispMs;
                }
                break;
            case PM_METRIC_DISPLAY_LATENCY:
                if (elem.dataSize >= sizeof(double)) {
                    double dispLatMs = (event.displayTimestampNs >= event.cpuStartTimestampNs && event.cpuStartTimestampNs > 0)
                        ? (static_cast<double>(event.displayTimestampNs - event.cpuStartTimestampNs) / 1'000'000.0)
                        : 0.0;
                    *reinterpret_cast<double*>(dest) = dispLatMs;
                }
                break;
            case PM_METRIC_CLICK_TO_PHOTON_LATENCY:
                if (elem.dataSize >= sizeof(double)) {
                    uint64_t lastClick = coordinator ? coordinator->GetLastClickTimestampNs() : 0;
                    double latencyMs = (lastClick > 0 && event.presentStartTimestampNs > lastClick)
                        ? (static_cast<double>(event.presentStartTimestampNs - lastClick) / 1'000'000.0)
                        : 0.0;
                    *reinterpret_cast<double*>(dest) = latencyMs;
                }
                break;
            case PM_METRIC_DROPPED_FRAMES:
                if (elem.dataSize >= sizeof(uint32_t)) {
                    *reinterpret_cast<uint32_t*>(dest) = event.dropped;
                }
                break;
            case PM_METRIC_GPU_VOLTAGE:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = gpu.voltageMv;
                }
                break;
            case PM_METRIC_GPU_MEM_FREQUENCY:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = gpu.memFrequencyMhz;
                }
                break;
            case PM_METRIC_GPU_MEM_UTILIZATION:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = gpu.memUtilizationPercent;
                }
                break;
            case PM_METRIC_GPU_MEM_USED:
                if (elem.dataSize >= sizeof(uint64_t)) {
                    *reinterpret_cast<uint64_t*>(dest) = gpu.vramUsedBytes;
                }
                break;
            case PM_METRIC_GPU_MEM_SIZE:
                if (elem.dataSize >= sizeof(uint64_t)) {
                    *reinterpret_cast<uint64_t*>(dest) = gpu.vramTotalBytes;
                }
                break;
            case PM_METRIC_GPU_FAN_SPEED:
                if (elem.dataSize >= sizeof(double)) {
                    *reinterpret_cast<double*>(dest) = gpu.fanSpeedRpm;
                }
                break;
            default:
                break;
            }
        }

        framesRead++;
    }

    *pNumFramesToRead = framesRead;
    return PM_STATUS_SUCCESS;
}

PRESENTMON_API2_EXPORT PM_STATUS pmFreeFrameQuery(PM_FRAME_QUERY_HANDLE handle) {
    if (!handle) return PM_STATUS_BAD_HANDLE;
    delete handle;
    return PM_STATUS_SUCCESS;
}

PRESENTMON_API2_EXPORT PM_STATUS pmCheckHotkeyTriggered(PM_SESSION_HANDLE handle, bool* pTriggered) {
    if (!handle || !pTriggered) return PM_STATUS_BAD_HANDLE;
    *pTriggered = handle->coordinator ? handle->coordinator->ConsumeHotkeyToggle() : false;
    return PM_STATUS_SUCCESS;
}

PRESENTMON_API2_EXPORT PM_STATUS pmCheckOverlayHotkeyTriggered(PM_SESSION_HANDLE handle, bool* pTriggered) {
    if (!handle || !pTriggered) return PM_STATUS_BAD_HANDLE;
    *pTriggered = handle->coordinator ? handle->coordinator->ConsumeOverlayHotkeyToggle() : false;
    return PM_STATUS_SUCCESS;
}

PRESENTMON_API2_EXPORT PM_STATUS pmCheckRecordHotkeyTriggered(PM_SESSION_HANDLE handle, bool* pTriggered) {
    if (!handle || !pTriggered) return PM_STATUS_BAD_HANDLE;
    *pTriggered = handle->coordinator ? handle->coordinator->ConsumeRecordHotkeyToggle() : false;
    return PM_STATUS_SUCCESS;
}

PRESENTMON_API2_EXPORT PM_STATUS pmCheckInGameOverlayHotkeyTriggered(PM_SESSION_HANDLE handle, bool* pTriggered) {
    if (!handle || !pTriggered) return PM_STATUS_BAD_HANDLE;
    *pTriggered = handle->coordinator ? handle->coordinator->ConsumeInGameHudHotkeyToggle() : false;
    return PM_STATUS_SUCCESS;
}

PRESENTMON_API2_EXPORT PM_STATUS pmCheckMiniHudHotkeyTriggered(PM_SESSION_HANDLE handle, bool* pTriggered) {
    if (!handle || !pTriggered) return PM_STATUS_BAD_HANDLE;
    *pTriggered = handle->coordinator ? handle->coordinator->ConsumeMiniHudHotkeyToggle() : false;
    return PM_STATUS_SUCCESS;
}

PRESENTMON_API2_EXPORT PM_STATUS pmSetHotkeys(PM_SESSION_HANDLE handle, const char* inGameHud, const char* record, const char* overlay, const char* miniHud) {
    if (!handle) return PM_STATUS_BAD_HANDLE;
    if (handle->coordinator) {
        handle->coordinator->SetHotkeys(inGameHud ? inGameHud : "",
                                        record ? record : "",
                                        overlay ? overlay : "",
                                        miniHud ? miniHud : "");
        return PM_STATUS_SUCCESS;
    }
    return PM_STATUS_SERVICE_ERROR;
}

PRESENTMON_API2_EXPORT PM_STATUS pmSetInGameOverlayState(PM_SESSION_HANDLE handle, bool enabled) {
    if (!handle) return PM_STATUS_BAD_HANDLE;
    if (handle->coordinator) {
        handle->coordinator->SetInGameOverlayEnabled(enabled);
        return PM_STATUS_SUCCESS;
    }
    return PM_STATUS_SERVICE_ERROR;
}

PRESENTMON_API2_EXPORT PM_STATUS pmGetInGameOverlayState(PM_SESSION_HANDLE handle, bool* pEnabled) {
    if (!handle || !pEnabled) return PM_STATUS_BAD_HANDLE;
    if (handle->coordinator) {
        *pEnabled = handle->coordinator->IsInGameOverlayEnabled();
        return PM_STATUS_SUCCESS;
    }
    return PM_STATUS_SERVICE_ERROR;
}

PRESENTMON_API2_EXPORT PM_STATUS pmSetRecordingState(PM_SESSION_HANDLE handle, bool recording) {
    if (!handle) return PM_STATUS_BAD_HANDLE;
    if (handle->coordinator) {
        handle->coordinator->SetRecordingState(recording);
        return PM_STATUS_SUCCESS;
    }
    return PM_STATUS_SERVICE_ERROR;
}

PRESENTMON_API2_EXPORT PM_STATUS pmGetRecordingState(PM_SESSION_HANDLE handle, bool* pRecording) {
    if (!handle || !pRecording) return PM_STATUS_BAD_HANDLE;
    if (handle->coordinator) {
        *pRecording = handle->coordinator->IsRecordingActive();
        return PM_STATUS_SUCCESS;
    }
    return PM_STATUS_SERVICE_ERROR;
}

} // extern "C"
