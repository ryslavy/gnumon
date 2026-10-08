#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include "../ipc/FrameRingBuffer.h"
#include "../common/Clock.h"

#include <unordered_map>
#include <mutex>
#include <cstring>
#include <iostream>

#ifndef VK_LAYER_EXPORT
#define VK_LAYER_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

// Dispatch table for instance
struct InstanceDispatch {
    PFN_vkGetInstanceProcAddr getProcAddr = nullptr;
    PFN_vkDestroyInstance destroyInstance = nullptr;
};

// Dispatch table for device
struct DeviceDispatch {
    PFN_vkGetDeviceProcAddr getProcAddr = nullptr;
    PFN_vkDestroyDevice destroyDevice = nullptr;
    PFN_vkQueuePresentKHR queuePresentKHR = nullptr;
    PFN_vkAcquireNextImageKHR acquireNextImageKHR = nullptr;
    PFN_vkAcquireNextImage2KHR acquireNextImage2KHR = nullptr;
    PFN_vkQueueSubmit queueSubmit = nullptr;
#ifdef VK_VERSION_1_3
    PFN_vkQueueSubmit2 queueSubmit2 = nullptr;
#endif
};

std::mutex g_dispatchMutex;
std::unordered_map<void*, InstanceDispatch> g_instanceDispatch;
std::unordered_map<void*, DeviceDispatch> g_deviceDispatch;

gnumon::ipc::FrameRingProducer g_producer;
std::atomic<uint32_t> g_frameId{0};
std::atomic<uint64_t> g_lastPresentStartNs{0};
std::atomic<uint64_t> g_currentGpuSubmitStartNs{0};
std::atomic<uint64_t> g_lastGpuSubmitEndNs{0};
thread_local uint64_t g_currentCpuStartNs = 0;
thread_local bool g_hasAcquiredImage = false;
static bool g_enableOverlay = (getenv("GNUMON_OVERLAY") != nullptr && std::string(getenv("GNUMON_OVERLAY")) != "0");

void* GetDispatchKey(const void* object) {
    return const_cast<void*>(object);
}

} // namespace

// --- Intercepted Functions ---

static VKAPI_ATTR VkResult VKAPI_CALL gnumon_vkCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance)
{
    auto* layerCreateInfo = const_cast<VkLayerInstanceCreateInfo*>(
        reinterpret_cast<const VkLayerInstanceCreateInfo*>(pCreateInfo->pNext));

    // Find link info in pNext chain
    while (layerCreateInfo && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO ||
                               layerCreateInfo->function != VK_LAYER_LINK_INFO)) {
        layerCreateInfo = const_cast<VkLayerInstanceCreateInfo*>(
            reinterpret_cast<const VkLayerInstanceCreateInfo*>(layerCreateInfo->pNext));
    }

    if (!layerCreateInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr nextGIPA = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

    auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(nextGIPA(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!createInstance) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = createInstance(pCreateInfo, pAllocator, pInstance);
    if (result == VK_SUCCESS && pInstance && *pInstance) {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        InstanceDispatch disp{};
        disp.getProcAddr = nextGIPA;
        disp.destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(nextGIPA(*pInstance, "vkDestroyInstance"));
        g_instanceDispatch[GetDispatchKey(*pInstance)] = disp;

        // Initialize frame ring buffer for this process
        g_producer.Open(getpid());
    }

    return result;
}

static VKAPI_ATTR void VKAPI_CALL gnumon_vkDestroyInstance(
    VkInstance instance,
    const VkAllocationCallbacks* pAllocator)
{
    PFN_vkDestroyInstance destroyInstance = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        auto it = g_instanceDispatch.find(GetDispatchKey(instance));
        if (it != g_instanceDispatch.end()) {
            destroyInstance = it->second.destroyInstance;
            g_instanceDispatch.erase(it);
        }
    }

    if (destroyInstance) {
        destroyInstance(instance, pAllocator);
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL gnumon_vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice)
{
    auto* layerCreateInfo = const_cast<VkLayerDeviceCreateInfo*>(
        reinterpret_cast<const VkLayerDeviceCreateInfo*>(pCreateInfo->pNext));

    while (layerCreateInfo && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO ||
                               layerCreateInfo->function != VK_LAYER_LINK_INFO)) {
        layerCreateInfo = const_cast<VkLayerDeviceCreateInfo*>(
            reinterpret_cast<const VkLayerDeviceCreateInfo*>(layerCreateInfo->pNext));
    }

    if (!layerCreateInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr nextGIPA = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr nextGDPA = layerCreateInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

    auto createDevice = reinterpret_cast<PFN_vkCreateDevice>(nextGIPA(VK_NULL_HANDLE, "vkCreateDevice"));
    if (!createDevice) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = createDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
    if (result == VK_SUCCESS && pDevice && *pDevice) {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        DeviceDispatch disp{};
        disp.getProcAddr = nextGDPA;
        disp.destroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(nextGDPA(*pDevice, "vkDestroyDevice"));
        disp.queuePresentKHR = reinterpret_cast<PFN_vkQueuePresentKHR>(nextGDPA(*pDevice, "vkQueuePresentKHR"));
        disp.acquireNextImageKHR = reinterpret_cast<PFN_vkAcquireNextImageKHR>(nextGDPA(*pDevice, "vkAcquireNextImageKHR"));
        disp.acquireNextImage2KHR = reinterpret_cast<PFN_vkAcquireNextImage2KHR>(nextGDPA(*pDevice, "vkAcquireNextImage2KHR"));
        disp.queueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(nextGDPA(*pDevice, "vkQueueSubmit"));
#ifdef VK_VERSION_1_3
        disp.queueSubmit2 = reinterpret_cast<PFN_vkQueueSubmit2>(nextGDPA(*pDevice, "vkQueueSubmit2"));
        if (!disp.queueSubmit2) {
            disp.queueSubmit2 = reinterpret_cast<PFN_vkQueueSubmit2>(nextGDPA(*pDevice, "vkQueueSubmit2KHR"));
        }
#endif
        g_deviceDispatch[GetDispatchKey(*pDevice)] = disp;
    }

    return result;
}

static VKAPI_ATTR void VKAPI_CALL gnumon_vkDestroyDevice(
    VkDevice device,
    const VkAllocationCallbacks* pAllocator)
{
    PFN_vkDestroyDevice destroyDevice = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        auto it = g_deviceDispatch.find(GetDispatchKey(device));
        if (it != g_deviceDispatch.end()) {
            destroyDevice = it->second.destroyDevice;
            g_deviceDispatch.erase(it);
        }
    }

    if (destroyDevice) {
        destroyDevice(device, pAllocator);
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL gnumon_vkAcquireNextImageKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    uint64_t timeout,
    VkSemaphore semaphore,
    VkFence fence,
    uint32_t* pImageIndex)
{
    g_currentCpuStartNs = gnumon::common::Clock::GetTimestampNs();
    g_hasAcquiredImage = true;

    PFN_vkAcquireNextImageKHR nextFunc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        auto it = g_deviceDispatch.find(GetDispatchKey(device));
        if (it != g_deviceDispatch.end()) {
            nextFunc = it->second.acquireNextImageKHR;
        }
    }

    if (nextFunc) {
        return nextFunc(device, swapchain, timeout, semaphore, fence, pImageIndex);
    }
    return VK_ERROR_INITIALIZATION_FAILED;
}

static VKAPI_ATTR VkResult VKAPI_CALL gnumon_vkAcquireNextImage2KHR(
    VkDevice device,
    const VkAcquireNextImageInfoKHR* pAcquireInfo,
    uint32_t* pImageIndex)
{
    g_currentCpuStartNs = gnumon::common::Clock::GetTimestampNs();
    g_hasAcquiredImage = true;

    PFN_vkAcquireNextImage2KHR nextFunc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        auto it = g_deviceDispatch.find(GetDispatchKey(device));
        if (it != g_deviceDispatch.end()) {
            nextFunc = it->second.acquireNextImage2KHR;
        }
    }

    if (nextFunc) {
        return nextFunc(device, pAcquireInfo, pImageIndex);
    }
    return VK_ERROR_INITIALIZATION_FAILED;
}

static VKAPI_ATTR VkResult VKAPI_CALL gnumon_vkQueueSubmit(
    VkQueue queue,
    uint32_t submitCount,
    const VkSubmitInfo* pSubmits,
    VkFence fence)
{
    uint64_t submitTime = gnumon::common::Clock::GetTimestampNs();
    uint64_t expectedZero = 0;
    g_currentGpuSubmitStartNs.compare_exchange_strong(expectedZero, submitTime, std::memory_order_acq_rel);

    PFN_vkQueueSubmit nextFunc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        for (const auto& [dev, disp] : g_deviceDispatch) {
            if (disp.queueSubmit) {
                nextFunc = disp.queueSubmit;
                break;
            }
        }
    }

    VkResult res = VK_SUCCESS;
    if (nextFunc) {
        res = nextFunc(queue, submitCount, pSubmits, fence);
    }
    uint64_t submitEnd = gnumon::common::Clock::GetTimestampNs();
    g_lastGpuSubmitEndNs.store(submitEnd, std::memory_order_release);
    return res;
}

#ifdef VK_VERSION_1_3
static VKAPI_ATTR VkResult VKAPI_CALL gnumon_vkQueueSubmit2(
    VkQueue queue,
    uint32_t submitCount,
    const VkSubmitInfo2* pSubmits,
    VkFence fence)
{
    uint64_t submitTime = gnumon::common::Clock::GetTimestampNs();
    uint64_t expectedZero = 0;
    g_currentGpuSubmitStartNs.compare_exchange_strong(expectedZero, submitTime, std::memory_order_acq_rel);

    PFN_vkQueueSubmit2 nextFunc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        for (const auto& [dev, disp] : g_deviceDispatch) {
            if (disp.queueSubmit2) {
                nextFunc = disp.queueSubmit2;
                break;
            }
        }
    }

    VkResult res = VK_SUCCESS;
    if (nextFunc) {
        res = nextFunc(queue, submitCount, pSubmits, fence);
    }
    uint64_t submitEnd = gnumon::common::Clock::GetTimestampNs();
    g_lastGpuSubmitEndNs.store(submitEnd, std::memory_order_release);
    return res;
}
#endif

static VKAPI_ATTR VkResult VKAPI_CALL gnumon_vkQueuePresentKHR(
    VkQueue queue,
    const VkPresentInfoKHR* pPresentInfo)
{
    uint64_t presentStartNs = gnumon::common::Clock::GetTimestampNs();

    PFN_vkQueuePresentKHR nextFunc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        for (const auto& [dev, disp] : g_deviceDispatch) {
            if (disp.queuePresentKHR) {
                nextFunc = disp.queuePresentKHR;
                break;
            }
        }
    }

    VkResult result = VK_SUCCESS;
    if (nextFunc) {
        result = nextFunc(queue, pPresentInfo);
    }

    uint64_t presentEndNs = gnumon::common::Clock::GetTimestampNs();
    uint64_t lastStart = g_lastPresentStartNs.exchange(presentStartNs, std::memory_order_acq_rel);
    uint64_t frameTimeNs = (lastStart > 0 && presentStartNs > lastStart) ? (presentStartNs - lastStart) : 0;

    uint64_t gpuStart = g_currentGpuSubmitStartNs.exchange(0, std::memory_order_acq_rel);
    if (gpuStart == 0) {
        gpuStart = (g_currentCpuStartNs > 0) ? g_currentCpuStartNs : lastStart;
    }
    uint64_t lastSubmitEnd = g_lastGpuSubmitEndNs.load(std::memory_order_acquire);
    uint64_t gpuDuration = (presentStartNs > gpuStart) ? (presentStartNs - gpuStart) : 0;
    uint64_t gpuBusy = (lastSubmitEnd > gpuStart && lastSubmitEnd <= presentStartNs)
        ? (lastSubmitEnd - gpuStart)
        : gpuDuration;
    uint64_t gpuWait = (gpuDuration > gpuBusy) ? (gpuDuration - gpuBusy) : 0;

    gnumon::ipc::FrameEvent event{};
    event.processId = static_cast<uint32_t>(getpid());
    event.frameId = ++g_frameId;
    event.cpuStartTimestampNs = g_currentCpuStartNs;
    event.presentStartTimestampNs = presentStartNs;
    event.presentDurationNs = presentEndNs - presentStartNs;
    event.frameTimeNs = frameTimeNs;
    if (pPresentInfo && pPresentInfo->swapchainCount > 0) {
        event.swapChain = reinterpret_cast<uint64_t>(pPresentInfo->pSwapchains[0]);
    }
    event.flags = static_cast<uint32_t>(result);

    event.gpuStartTimestampNs = gpuStart;
    event.gpuDurationNs = gpuDuration;
    event.gpuBusyNs = gpuBusy;
    event.gpuWaitNs = gpuWait;
    event.displayTimestampNs = presentEndNs;
    event.dropped = (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) ? 1 : 0;

    // Frame Generation Detection (FSR3 / AFMF / XeFG)
    bool isGenerated = !g_hasAcquiredImage;
    g_hasAcquiredImage = false;

    static bool hasAfmfEnv = (getenv("AMD_AFMF") != nullptr || getenv("FSR_FRAME_GEN") != nullptr);
    static bool hasXefgEnv = (getenv("INTEL_XEFG") != nullptr);

    uint32_t fType = 2; // PM_FRAME_TYPE_APPLICATION
    if (isGenerated) {
        if (hasAfmfEnv) fType = 100; // PM_FRAME_TYPE_AMD_AFMF
        else if (hasXefgEnv) fType = 50; // PM_FRAME_TYPE_INTEL_XEFG
        else fType = 3; // PM_FRAME_TYPE_REPEATED
    }
    event.frameType = fType;

    bool overlayActive = g_enableOverlay || g_producer.IsOverlayEnabled();
    bool isRec = g_producer.IsRecordingActive();

    if (overlayActive && (event.frameId % 60 == 0)) {
        double fps = (frameTimeNs > 0) ? (1'000'000'000.0 / static_cast<double>(frameTimeNs)) : 0.0;
        double ftMs = static_cast<double>(frameTimeNs) / 1'000'000.0;
        double gpuMs = static_cast<double>(gpuDuration) / 1'000'000.0;
        std::cerr << "[gnumon overlay]"
                  << (isRec ? " [● REC]" : "")
                  << " FPS: " << static_cast<int>(fps)
                  << " | FT: " << ftMs << " ms"
                  << " | GPU: " << gpuMs << " ms"
                  << " | Type: " << (fType == 100 ? "AMD AFMF" : (fType == 50 ? "Intel XeFG" : (fType == 3 ? "Repeated" : "App")))
                  << std::endl;
    }

    g_producer.Push(event);

    return result;
}

// --- ProcAddr Interceptors ---

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL gnumon_vkGetDeviceProcAddr(
    VkDevice device,
    const char* pName)
{
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkGetDeviceProcAddr);
    if (std::strcmp(pName, "vkDestroyDevice") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkDestroyDevice);
    if (std::strcmp(pName, "vkQueuePresentKHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkQueuePresentKHR);
    if (std::strcmp(pName, "vkAcquireNextImageKHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkAcquireNextImageKHR);
    if (std::strcmp(pName, "vkAcquireNextImage2KHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkAcquireNextImage2KHR);
    if (std::strcmp(pName, "vkQueueSubmit") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkQueueSubmit);
#ifdef VK_VERSION_1_3
    if (std::strcmp(pName, "vkQueueSubmit2") == 0 || std::strcmp(pName, "vkQueueSubmit2KHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkQueueSubmit2);
#endif

    PFN_vkGetDeviceProcAddr nextGDPA = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        auto it = g_deviceDispatch.find(GetDispatchKey(device));
        if (it != g_deviceDispatch.end()) {
            nextGDPA = it->second.getProcAddr;
        }
    }

    if (nextGDPA) {
        return nextGDPA(device, pName);
    }
    return nullptr;
}

VK_LAYER_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL gnumon_vkGetInstanceProcAddr(
    VkInstance instance,
    const char* pName)
{
    if (std::strcmp(pName, "vkGetInstanceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkGetInstanceProcAddr);
    if (std::strcmp(pName, "vkCreateInstance") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkCreateInstance);
    if (std::strcmp(pName, "vkDestroyInstance") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkDestroyInstance);
    if (std::strcmp(pName, "vkCreateDevice") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkCreateDevice);
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkGetDeviceProcAddr);
    if (std::strcmp(pName, "vkQueuePresentKHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkQueuePresentKHR);
    if (std::strcmp(pName, "vkAcquireNextImageKHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkAcquireNextImageKHR);
    if (std::strcmp(pName, "vkAcquireNextImage2KHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkAcquireNextImage2KHR);
    if (std::strcmp(pName, "vkQueueSubmit") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkQueueSubmit);
#ifdef VK_VERSION_1_3
    if (std::strcmp(pName, "vkQueueSubmit2") == 0 || std::strcmp(pName, "vkQueueSubmit2KHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkQueueSubmit2);
#endif

    PFN_vkGetInstanceProcAddr nextGIPA = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        auto it = g_instanceDispatch.find(GetDispatchKey(instance));
        if (it != g_instanceDispatch.end()) {
            nextGIPA = it->second.getProcAddr;
        }
    }

    if (nextGIPA) {
        return nextGIPA(instance, pName);
    }
    return nullptr;
}

// --- Layer Negotiation ---

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(
    VkNegotiateLayerInterface* pVersionStruct)
{
    if (!pVersionStruct || pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (pVersionStruct->loaderLayerInterfaceVersion >= 2) {
        pVersionStruct->loaderLayerInterfaceVersion = 2;
    } else if (pVersionStruct->loaderLayerInterfaceVersion == 1) {
        pVersionStruct->loaderLayerInterfaceVersion = 1;
    } else {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    pVersionStruct->pfnGetInstanceProcAddr = gnumon_vkGetInstanceProcAddr;
    pVersionStruct->pfnGetDeviceProcAddr = gnumon_vkGetDeviceProcAddr;
    pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;

    return VK_SUCCESS;
}
