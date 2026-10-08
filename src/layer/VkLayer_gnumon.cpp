#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include "../ipc/FrameRingBuffer.h"
#include "../common/Clock.h"
#include "VulkanOverlayRenderer.h"

#include <unordered_map>
#include <mutex>
#include <cstring>
#include <iostream>
#include <fstream>
#include <deque>
#include <algorithm>

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
    PFN_vkCreateSwapchainKHR createSwapchainKHR = nullptr;
    PFN_vkDestroySwapchainKHR destroySwapchainKHR = nullptr;
    PFN_vkGetSwapchainImagesKHR getSwapchainImagesKHR = nullptr;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    PFN_vkGetInstanceProcAddr getInstProcAddr = nullptr;
};

struct SwapchainInfo {
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{0, 0};
    std::vector<VkImage> images;
};

std::mutex g_dispatchMutex;
std::unordered_map<void*, InstanceDispatch> g_instanceDispatch;
std::unordered_map<void*, DeviceDispatch> g_deviceDispatch;

std::mutex g_swapchainMutex;
std::unordered_map<VkSwapchainKHR, SwapchainInfo> g_swapchains;
gnumon::layer::VulkanOverlayRenderer g_overlayRenderer;

struct HudHistory {
    std::deque<double> fps;
    uint64_t lastDisplayNs = 0;
    uint64_t lastCpuStartNs = 0;
};
HudHistory g_hudHistory;
std::mutex g_hudMutex;

gnumon::ipc::FrameRingProducer g_producer;
std::atomic<uint32_t> g_frameId{0};
std::atomic<uint64_t> g_lastPresentStartNs{0};
std::atomic<uint64_t> g_currentGpuSubmitStartNs{0};
std::atomic<uint64_t> g_lastGpuSubmitEndNs{0};
static int GetConfiguredHudCorner() {
    const char* envCorner = getenv("GNUMON_CORNER");
    if (envCorner) {
        std::string s(envCorner);
        if (s == "1" || s == "TR" || s == "top-right") return 1;
        if (s == "2" || s == "BL" || s == "bottom-left") return 2;
        if (s == "3" || s == "BR" || s == "bottom-right") return 3;
        return 0;
    }
    const char* home = getenv("HOME");
    if (home) {
        std::string cfgPath = std::string(home) + "/.config/gnumon/config.ini";
        std::ifstream file(cfgPath);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                if (line.rfind("inGameHudCorner", 0) == 0) {
                    auto pos = line.find('=');
                    if (pos != std::string::npos) {
                        try { return std::stoi(line.substr(pos + 1)); } catch (...) {}
                    }
                }
            }
        }
    }
    return 0; // Top-Left default
}

static bool GetConfiguredHudDefault() {
    const char* envOverlay = getenv("GNUMON_OVERLAY");
    if (envOverlay) {
        return (std::string(envOverlay) != "0");
    }
    const char* home = getenv("HOME");
    if (home) {
        std::string cfgPath = std::string(home) + "/.config/gnumon/config.ini";
        std::ifstream file(cfgPath);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                if (line.rfind("inGameHudEnabled", 0) == 0) {
                    auto pos = line.find('=');
                    if (pos != std::string::npos) {
                        std::string val = line.substr(pos + 1);
                        return (val == "true" || val == "1");
                    }
                }
            }
        }
    }
    return false;
}

static bool g_enableOverlay = GetConfiguredHudDefault();
static int g_hudCorner = GetConfiguredHudCorner();
thread_local uint64_t g_currentCpuStartNs = 0;
thread_local bool g_hasAcquiredImage = false;

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
        disp.createSwapchainKHR = reinterpret_cast<PFN_vkCreateSwapchainKHR>(nextGDPA(*pDevice, "vkCreateSwapchainKHR"));
        disp.destroySwapchainKHR = reinterpret_cast<PFN_vkDestroySwapchainKHR>(nextGDPA(*pDevice, "vkDestroySwapchainKHR"));
        disp.getSwapchainImagesKHR = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(nextGDPA(*pDevice, "vkGetSwapchainImagesKHR"));
        disp.physicalDevice = physicalDevice;
        disp.getInstProcAddr = nextGIPA;
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

static VKAPI_ATTR VkResult VKAPI_CALL gnumon_vkCreateSwapchainKHR(
    VkDevice device,
    const VkSwapchainCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSwapchainKHR* pSwapchain)
{
    VkSwapchainCreateInfoKHR createInfo = *pCreateInfo;
    // Add transfer destination usage so HUD overlay can be copied to swapchain image
    createInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    PFN_vkCreateSwapchainKHR nextFunc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        auto it = g_deviceDispatch.find(GetDispatchKey(device));
        if (it != g_deviceDispatch.end()) {
            nextFunc = it->second.createSwapchainKHR;
        }
    }

    VkResult res = VK_ERROR_INITIALIZATION_FAILED;
    if (nextFunc) {
        res = nextFunc(device, &createInfo, pAllocator, pSwapchain);
        if (res != VK_SUCCESS) {
            // Fallback to original createInfo if driver rejected TRANSFER_DST_BIT
            res = nextFunc(device, pCreateInfo, pAllocator, pSwapchain);
        }
    }

    if (res == VK_SUCCESS && pSwapchain && *pSwapchain) {
        std::lock_guard<std::mutex> lock(g_swapchainMutex);
        SwapchainInfo info{};
        info.swapchain = *pSwapchain;
        info.device = device;
        info.format = pCreateInfo->imageFormat;
        info.extent = pCreateInfo->imageExtent;

        PFN_vkGetSwapchainImagesKHR getImages = nullptr;
        {
            std::lock_guard<std::mutex> dlock(g_dispatchMutex);
            auto it = g_deviceDispatch.find(GetDispatchKey(device));
            if (it != g_deviceDispatch.end()) {
                getImages = it->second.getSwapchainImagesKHR;
            }
        }
        if (getImages) {
            uint32_t count = 0;
            if (getImages(device, *pSwapchain, &count, nullptr) == VK_SUCCESS && count > 0) {
                info.images.resize(count);
                getImages(device, *pSwapchain, &count, info.images.data());
            }
        }
        g_swapchains[*pSwapchain] = info;
    }
    return res;
}

static VKAPI_ATTR void VKAPI_CALL gnumon_vkDestroySwapchainKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    const VkAllocationCallbacks* pAllocator)
{
    {
        std::lock_guard<std::mutex> lock(g_swapchainMutex);
        g_swapchains.erase(swapchain);
    }

    PFN_vkDestroySwapchainKHR nextFunc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        auto it = g_deviceDispatch.find(GetDispatchKey(device));
        if (it != g_deviceDispatch.end()) {
            nextFunc = it->second.destroySwapchainKHR;
        }
    }

    if (nextFunc) {
        nextFunc(device, swapchain, pAllocator);
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
    bool overlayActive = g_enableOverlay || g_producer.IsOverlayEnabled();
    bool isRec = g_producer.IsRecordingActive();

    // Render In-Game HUD into swapchain image before presenting
    if (overlayActive && pPresentInfo && pPresentInfo->swapchainCount > 0) {
        VkSwapchainKHR sc = pPresentInfo->pSwapchains[0];
        uint32_t imgIdx = (pPresentInfo->pImageIndices) ? pPresentInfo->pImageIndices[0] : 0;

        VkDevice dev = VK_NULL_HANDLE;
        VkFormat fmt = VK_FORMAT_B8G8R8A8_UNORM;
        VkImage img = VK_NULL_HANDLE;
        uint32_t scWidth = 1920;
        uint32_t scHeight = 1080;
        {
            std::lock_guard<std::mutex> slock(g_swapchainMutex);
            auto it = g_swapchains.find(sc);
            if (it != g_swapchains.end()) {
                dev = it->second.device;
                fmt = it->second.format;
                if (it->second.extent.width > 0) scWidth = it->second.extent.width;
                if (it->second.extent.height > 0) scHeight = it->second.extent.height;
                if (imgIdx < it->second.images.size()) {
                    img = it->second.images[imgIdx];
                }
            }
        }

        if (dev && img) {
            if (!g_overlayRenderer.IsInitialized()) {
                std::lock_guard<std::mutex> dlock(g_dispatchMutex);
                auto dit = g_deviceDispatch.find(GetDispatchKey(dev));
                if (dit != g_deviceDispatch.end()) {
                    g_overlayRenderer.Initialize(dev, dit->second.physicalDevice, 0,
                                                 dit->second.getProcAddr,
                                                 dit->second.getInstProcAddr, VK_NULL_HANDLE);
                }
            }

            if (g_overlayRenderer.IsInitialized()) {
                double presentFps = 0.0;
                double ftMs = 0.0;
                double lowFps = 0.0;
                double dispFps = 0.0;
                double animErrMs = 0.0;
                double latMs = 0.0;

                {
                    std::lock_guard<std::mutex> hlock(g_hudMutex);
                    if (!g_hudHistory.fps.empty()) {
                        presentFps = g_hudHistory.fps.back();
                        ftMs = (presentFps > 0) ? (1000.0 / presentFps) : 0.0;
                        std::vector<double> sorted = {g_hudHistory.fps.begin(), g_hudHistory.fps.end()};
                        std::sort(sorted.begin(), sorted.end());
                        size_t idx = static_cast<size_t>(std::floor(sorted.size() * 0.01));
                        lowFps = sorted[std::min(idx, sorted.size() - 1)];
                    }
                    dispFps = presentFps;
                    if (g_hudHistory.lastDisplayNs > 0 && presentStartNs > g_hudHistory.lastDisplayNs) {
                        uint64_t deltaDispNs = presentStartNs - g_hudHistory.lastDisplayNs;
                        dispFps = 1'000'000'000.0 / static_cast<double>(deltaDispNs);
                        uint64_t deltaAppNs = (g_currentCpuStartNs > g_hudHistory.lastCpuStartNs && g_hudHistory.lastCpuStartNs > 0)
                            ? (g_currentCpuStartNs - g_hudHistory.lastCpuStartNs)
                            : deltaDispNs;
                        animErrMs = std::abs(static_cast<double>(deltaDispNs) - static_cast<double>(deltaAppNs)) / 1'000'000.0;
                    }
                    latMs = (g_currentCpuStartNs > 0 && presentStartNs > g_currentCpuStartNs)
                        ? (static_cast<double>(presentStartNs - g_currentCpuStartNs) / 1'000'000.0)
                        : ftMs;
                }

                g_overlayRenderer.RenderHud(queue, img, fmt,
                                            presentFps, dispFps, lowFps,
                                            ftMs, latMs, animErrMs, isRec,
                                            g_hudCorner, scWidth, scHeight);
            }
        }
    }

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
    double currentFps = (frameTimeNs > 0) ? (1'000'000'000.0 / static_cast<double>(frameTimeNs)) : 0.0;

    {
        std::lock_guard<std::mutex> hlock(g_hudMutex);
        if (currentFps > 0.0) {
            g_hudHistory.fps.push_back(currentFps);
            if (g_hudHistory.fps.size() > 120) {
                g_hudHistory.fps.pop_front();
            }
        }
        g_hudHistory.lastDisplayNs = presentEndNs;
        g_hudHistory.lastCpuStartNs = g_currentCpuStartNs;
    }

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
    if (std::strcmp(pName, "vkCreateSwapchainKHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkCreateSwapchainKHR);
    if (std::strcmp(pName, "vkDestroySwapchainKHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkDestroySwapchainKHR);
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
    if (std::strcmp(pName, "vkCreateSwapchainKHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkCreateSwapchainKHR);
    if (std::strcmp(pName, "vkDestroySwapchainKHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkDestroySwapchainKHR);
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
