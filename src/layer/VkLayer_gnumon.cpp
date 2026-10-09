#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#if !defined(_WIN32)
#include <dlfcn.h>
#endif
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
#include <linux/input.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

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
    PFN_vkGetDeviceQueue getDeviceQueue = nullptr;
    PFN_vkGetDeviceQueue2 getDeviceQueue2 = nullptr;
    PFN_vkCreateGraphicsPipelines createGraphicsPipelines = nullptr;
    PFN_vkCreateComputePipelines createComputePipelines = nullptr;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    PFN_vkGetInstanceProcAddr getInstProcAddr = nullptr;
    uint32_t defaultQueueFamilyIndex = 0;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamilyIndex = 0;
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

std::mutex g_queueMutex;
std::unordered_map<VkQueue, uint32_t> g_queueFamilyMap;
static PFN_vkGetPhysicalDeviceMemoryProperties g_getPhysicalDeviceMemoryProperties = nullptr;

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
std::atomic<uint32_t> g_psoCompileCount{0};
std::atomic<uint64_t> g_psoCompileDurationNs{0};
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

static bool IsProcessBlacklisted() {
    static int cachedResult = -1;
    if (cachedResult != -1) return (cachedResult == 1);

    if (getenv("DISABLE_GNUMON") != nullptr) {
        cachedResult = 1;
        return true;
    }

    std::string comm;
    std::ifstream commFile("/proc/self/comm");
    if (commFile.is_open()) {
        std::getline(commFile, comm);
        while (!comm.empty() && (comm.back() == '\r' || comm.back() == '\n' || comm.back() == ' ')) {
            comm.pop_back();
        }
    }

    static const char* blacklistedNames[] = {
        "gnome-shell",
        "kwin_wayland",
        "kwin_x11",
        "kwin",
        "hyprland",
        "Hyprland",
        "sway",
        "wayfire",
        "weston",
        "Xwayland",
        "plasmashell",
        "plasma-workspac",
        "gnumon-gui",
        "gnumond",
        "gnumon-cli",
        "steam",
        "steamwebhelper",
        "discord",
        "slack",
        "obs",
        "gamescope"
    };

    for (const char* name : blacklistedNames) {
        if (comm == name) {
            cachedResult = 1;
            return true;
        }
    }

    std::ifstream cmdFile("/proc/self/cmdline");
    if (cmdFile.is_open()) {
        std::string cmd;
        if (std::getline(cmdFile, cmd, '\0')) {
            for (const char* name : blacklistedNames) {
                if (cmd.find(name) != std::string::npos) {
                    cachedResult = 1;
                    return true;
                }
            }
        }
    }

    cachedResult = 0;
    return false;
}

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

        if (IsProcessBlacklisted()) {
            return result;
        }

        if (!g_getPhysicalDeviceMemoryProperties) {
            g_getPhysicalDeviceMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
                nextGIPA(*pInstance, "vkGetPhysicalDeviceMemoryProperties"));
            if (!g_getPhysicalDeviceMemoryProperties) {
#if !defined(_WIN32)
                g_getPhysicalDeviceMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
                    dlsym(RTLD_DEFAULT, "vkGetPhysicalDeviceMemoryProperties"));
#endif
            }
        }

        // Initialize frame ring buffer for this process
        g_producer.Open(getpid());
        if (g_enableOverlay) {
            g_producer.SetOverlayEnabled(true);
        }
        if (getenv("GNUMON_DEBUG") || getenv("GNUMON_OVERLAY")) {
            fprintf(stderr, "[gnumon-layer] vkCreateInstance initialized! PID=%d overlayDefault=%d\n",
                    getpid(), static_cast<int>(g_enableOverlay));
        }
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

        if (IsProcessBlacklisted()) {
            g_deviceDispatch[GetDispatchKey(*pDevice)] = disp;
            return result;
        }
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
        disp.getDeviceQueue = reinterpret_cast<PFN_vkGetDeviceQueue>(nextGDPA(*pDevice, "vkGetDeviceQueue"));
        disp.getDeviceQueue2 = reinterpret_cast<PFN_vkGetDeviceQueue2>(nextGDPA(*pDevice, "vkGetDeviceQueue2"));
        disp.createGraphicsPipelines = reinterpret_cast<PFN_vkCreateGraphicsPipelines>(nextGDPA(*pDevice, "vkCreateGraphicsPipelines"));
        disp.createComputePipelines = reinterpret_cast<PFN_vkCreateComputePipelines>(nextGDPA(*pDevice, "vkCreateComputePipelines"));
        disp.physicalDevice = physicalDevice;
        disp.getInstProcAddr = nextGIPA;
        if (pCreateInfo && pCreateInfo->queueCreateInfoCount > 0 && pCreateInfo->pQueueCreateInfos) {
            disp.defaultQueueFamilyIndex = pCreateInfo->pQueueCreateInfos[0].queueFamilyIndex;
        }

        // Detect graphics queue family
        PFN_vkGetPhysicalDeviceQueueFamilyProperties getQFamProps = nullptr;
        if (nextGIPA) {
            getQFamProps = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
                nextGIPA(VK_NULL_HANDLE, "vkGetPhysicalDeviceQueueFamilyProperties"));
        }
        uint32_t gfxQFam = disp.defaultQueueFamilyIndex;
        if (getQFamProps) {
            uint32_t qCount = 0;
            getQFamProps(physicalDevice, &qCount, nullptr);
            if (qCount > 0) {
                std::vector<VkQueueFamilyProperties> qProps(qCount);
                getQFamProps(physicalDevice, &qCount, qProps.data());
                for (uint32_t i = 0; i < qCount; ++i) {
                    if (qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                        gfxQFam = i;
                        break;
                    }
                }
            }
        }
        disp.graphicsQueueFamilyIndex = gfxQFam;
        if (disp.getDeviceQueue) {
            disp.getDeviceQueue(*pDevice, gfxQFam, 0, &disp.graphicsQueue);
        }
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

static VKAPI_ATTR void VKAPI_CALL gnumon_vkGetDeviceQueue(
    VkDevice device,
    uint32_t queueFamilyIndex,
    uint32_t queueIndex,
    VkQueue* pQueue)
{
    PFN_vkGetDeviceQueue nextFunc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        auto it = g_deviceDispatch.find(GetDispatchKey(device));
        if (it != g_deviceDispatch.end()) {
            nextFunc = it->second.getDeviceQueue;
        }
    }
    if (nextFunc) {
        nextFunc(device, queueFamilyIndex, queueIndex, pQueue);
        if (pQueue && *pQueue) {
            {
                std::lock_guard<std::mutex> qlock(g_queueMutex);
                g_queueFamilyMap[*pQueue] = queueFamilyIndex;
            }
            std::lock_guard<std::mutex> dlock(g_dispatchMutex);
            auto it = g_deviceDispatch.find(GetDispatchKey(device));
            if (it != g_deviceDispatch.end()) {
                if (queueFamilyIndex == it->second.graphicsQueueFamilyIndex) {
                    it->second.graphicsQueue = *pQueue;
                }
            }
        }
    }
}

static VKAPI_ATTR void VKAPI_CALL gnumon_vkGetDeviceQueue2(
    VkDevice device,
    const VkDeviceQueueInfo2* pQueueInfo,
    VkQueue* pQueue)
{
    PFN_vkGetDeviceQueue2 nextFunc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        auto it = g_deviceDispatch.find(GetDispatchKey(device));
        if (it != g_deviceDispatch.end()) {
            nextFunc = it->second.getDeviceQueue2;
        }
    }
    if (nextFunc) {
        nextFunc(device, pQueueInfo, pQueue);
        if (pQueue && *pQueue && pQueueInfo) {
            {
                std::lock_guard<std::mutex> qlock(g_queueMutex);
                g_queueFamilyMap[*pQueue] = pQueueInfo->queueFamilyIndex;
            }
            std::lock_guard<std::mutex> dlock(g_dispatchMutex);
            auto it = g_deviceDispatch.find(GetDispatchKey(device));
            if (it != g_deviceDispatch.end()) {
                if (pQueueInfo->queueFamilyIndex == it->second.graphicsQueueFamilyIndex) {
                    it->second.graphicsQueue = *pQueue;
                }
            }
        }
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL gnumon_vkGetSwapchainImagesKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    uint32_t* pSwapchainImageCount,
    VkImage* pSwapchainImages)
{
    PFN_vkGetSwapchainImagesKHR nextFunc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        auto it = g_deviceDispatch.find(GetDispatchKey(device));
        if (it != g_deviceDispatch.end()) {
            nextFunc = it->second.getSwapchainImagesKHR;
        }
    }
    VkResult res = VK_ERROR_INITIALIZATION_FAILED;
    if (nextFunc) {
        res = nextFunc(device, swapchain, pSwapchainImageCount, pSwapchainImages);
    }
    if (res == VK_SUCCESS && pSwapchainImages && pSwapchainImageCount && *pSwapchainImageCount > 0) {
        std::lock_guard<std::mutex> slock(g_swapchainMutex);
        auto it = g_swapchains.find(swapchain);
        if (it != g_swapchains.end()) {
            it->second.images.assign(pSwapchainImages, pSwapchainImages + *pSwapchainImageCount);
            if (it->second.extent.width > 0 && it->second.extent.height > 0) {
                g_overlayRenderer.OnSwapchainCreated(swapchain, it->second.format, it->second.extent,
                                                     *pSwapchainImageCount, pSwapchainImages);
            }
        }
    }
    return res;
}


static VKAPI_ATTR VkResult VKAPI_CALL gnumon_vkCreateSwapchainKHR(
    VkDevice device,
    const VkSwapchainCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSwapchainKHR* pSwapchain)
{
    PFN_vkCreateSwapchainKHR nextFunc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        auto it = g_deviceDispatch.find(GetDispatchKey(device));
        if (it != g_deviceDispatch.end()) {
            nextFunc = it->second.createSwapchainKHR;
        }
    }

    VkResult res = VK_ERROR_INITIALIZATION_FAILED;
    if (nextFunc && pCreateInfo) {
        VkSwapchainCreateInfoKHR createInfo = *pCreateInfo;
        createInfo.imageUsage |= (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        res = nextFunc(device, &createInfo, pAllocator, pSwapchain);
        if (res != VK_SUCCESS) {
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
                g_overlayRenderer.OnSwapchainCreated(*pSwapchain, info.format, info.extent,
                                                     count, info.images.data());
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
    g_overlayRenderer.OnSwapchainDestroyed(swapchain);

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

static VKAPI_ATTR VkResult VKAPI_CALL gnumon_vkCreateGraphicsPipelines(
    VkDevice device,
    VkPipelineCache pipelineCache,
    uint32_t createInfoCount,
    const VkGraphicsPipelineCreateInfo* pCreateInfos,
    const VkAllocationCallbacks* pAllocator,
    VkPipeline* pPipelines)
{
    uint64_t start = gnumon::common::Clock::GetTimestampNs();
    PFN_vkCreateGraphicsPipelines nextFunc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        auto it = g_deviceDispatch.find(GetDispatchKey(device));
        if (it != g_deviceDispatch.end()) {
            nextFunc = it->second.createGraphicsPipelines;
        }
    }
    VkResult res = VK_ERROR_INITIALIZATION_FAILED;
    if (nextFunc) {
        res = nextFunc(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
    }
    uint64_t duration = gnumon::common::Clock::GetTimestampNs() - start;
    g_psoCompileCount.fetch_add(createInfoCount, std::memory_order_relaxed);
    g_psoCompileDurationNs.fetch_add(duration, std::memory_order_relaxed);
    return res;
}

static VKAPI_ATTR VkResult VKAPI_CALL gnumon_vkCreateComputePipelines(
    VkDevice device,
    VkPipelineCache pipelineCache,
    uint32_t createInfoCount,
    const VkComputePipelineCreateInfo* pCreateInfos,
    const VkAllocationCallbacks* pAllocator,
    VkPipeline* pPipelines)
{
    uint64_t start = gnumon::common::Clock::GetTimestampNs();
    PFN_vkCreateComputePipelines nextFunc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        auto it = g_deviceDispatch.find(GetDispatchKey(device));
        if (it != g_deviceDispatch.end()) {
            nextFunc = it->second.createComputePipelines;
        }
    }
    VkResult res = VK_ERROR_INITIALIZATION_FAILED;
    if (nextFunc) {
        res = nextFunc(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
    }
    uint64_t duration = gnumon::common::Clock::GetTimestampNs() - start;
    g_psoCompileCount.fetch_add(createInfoCount, std::memory_order_relaxed);
    g_psoCompileDurationNs.fetch_add(duration, std::memory_order_relaxed);
    return res;
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

static void CheckInGameHotkeys(uint64_t nowNs) {
    static uint64_t lastCheckNs = 0;
    if (nowNs < lastCheckNs + 33'000'000) {
        return; // Check at most once every 33 ms (~30 Hz)
    }
    lastCheckNs = nowNs;

    // 1. Evdev hardware input polling (works globally: Wayland, Gamescope, Fullscreen)
    static std::vector<int> evdevFds;
    static bool evdevScanned = false;
    if (!evdevScanned) {
        evdevScanned = true;
        DIR* dir = opendir("/dev/input");
        if (dir) {
            struct dirent* ent;
            while ((ent = readdir(dir)) != nullptr) {
                if (strncmp(ent->d_name, "event", 5) == 0) {
                    std::string path = std::string("/dev/input/") + ent->d_name;
                    int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                    if (fd >= 0) {
                        evdevFds.push_back(fd);
                    }
                }
            }
            closedir(dir);
        }
    }
    for (int efd : evdevFds) {
        struct input_event iev[8];
        ssize_t n = read(efd, iev, sizeof(iev));
        if (n > 0) {
            size_t count = n / sizeof(struct input_event);
            for (size_t i = 0; i < count; ++i) {
                if (iev[i].type == EV_KEY && iev[i].value == 1) {
                    if (iev[i].code == 66 /* KEY_F8 */) {
                        int nextPreset = (g_overlayRenderer.GetPreset() + 1) % 3;
                        g_overlayRenderer.SetPreset(nextPreset);
                        const char* presetNames[] = {"Compact", "Standard (Oscilloscope)", "Detailed"};
                        g_overlayRenderer.TriggerToast("Preset Changed", presetNames[nextPreset], 2.5f);
                    } else if (iev[i].code == 67 /* KEY_F9 */) {
                        g_enableOverlay = !g_enableOverlay;
                        g_producer.SetOverlayEnabled(g_enableOverlay);
                        g_overlayRenderer.TriggerToast("In-Game Overlay", g_enableOverlay ? "ENABLED" : "DISABLED", 2.0f);
                        if (getenv("GNUMON_DEBUG") || getenv("GNUMON_OVERLAY")) {
                            fprintf(stderr, "[gnumon-layer] Evdev hotkey F9 pressed! In-Game HUD: %s\n",
                                    g_enableOverlay ? "ON" : "OFF");
                        }
                    } else if (iev[i].code == 68 /* KEY_F10 */) {
                        bool newRec = !g_producer.IsRecordingActive();
                        g_producer.SetRecordingActive(newRec);
                        g_overlayRenderer.TriggerToast(newRec ? "Benchmark Capture" : "Capture Saved",
                                                       newRec ? "RECORDING STARTED" : "CSV BENCHMARK SAVED", 3.0f);
                        if (getenv("GNUMON_DEBUG") || getenv("GNUMON_OVERLAY")) {
                            fprintf(stderr, "[gnumon-layer] Evdev hotkey F10 pressed! Capture: %s\n",
                                    newRec ? "STARTING" : "STOPPED");
                        }
                    }
                }
            }
        }
    }

    // 2. X11 / Xwayland fallback
    typedef void* (*XOpenDisplay_fn)(const char*);
    typedef int (*XQueryKeymap_fn)(void*, char[32]);
    typedef unsigned char (*XKeysymToKeycode_fn)(void*, unsigned long);

    static void* x11Lib = nullptr;
    static XOpenDisplay_fn pXOpenDisplay = nullptr;
    static XQueryKeymap_fn pXQueryKeymap = nullptr;
    static XKeysymToKeycode_fn pXKeysymToKeycode = nullptr;
    static void* dpy = nullptr;
    static uint8_t kcF8 = 0;
    static uint8_t kcF9 = 0;
    static uint8_t kcF10 = 0;
    static uint64_t lastDpyTryNs = 0;

    if (!dpy && (nowNs - lastDpyTryNs > 1'000'000'000ULL)) {
        lastDpyTryNs = nowNs;
#if !defined(_WIN32)
        if (!x11Lib) {
            x11Lib = dlopen("libX11.so.6", RTLD_LAZY);
            if (!x11Lib) x11Lib = dlopen("libX11.so", RTLD_LAZY);
            if (x11Lib) {
                pXOpenDisplay = (XOpenDisplay_fn)dlsym(x11Lib, "XOpenDisplay");
                pXQueryKeymap = (XQueryKeymap_fn)dlsym(x11Lib, "XQueryKeymap");
                pXKeysymToKeycode = (XKeysymToKeycode_fn)dlsym(x11Lib, "XKeysymToKeycode");
            }
        }
        if (pXOpenDisplay) {
            dpy = pXOpenDisplay(nullptr);
            if (dpy && pXKeysymToKeycode) {
                kcF8 = pXKeysymToKeycode(dpy, 0xffc5 /* XK_F8 */);
                kcF9 = pXKeysymToKeycode(dpy, 0xffc6 /* XK_F9 */);
                kcF10 = pXKeysymToKeycode(dpy, 0xffc7 /* XK_F10 */);
            }
        }
#endif
    }

    if (!dpy || !pXQueryKeymap) return;

    char keys[32]{};
    pXQueryKeymap(dpy, keys);

    auto isDown = [&](uint8_t kc) -> bool {
        if (kc == 0) return false;
        return (keys[kc / 8] & (1 << (kc % 8))) != 0;
    };

    static bool wasF8 = false;
    bool downF8 = isDown(kcF8);
    if (downF8 && !wasF8) {
        int nextPreset = (g_overlayRenderer.GetPreset() + 1) % 3;
        g_overlayRenderer.SetPreset(nextPreset);
        const char* presetNames[] = {"Compact", "Standard (Oscilloscope)", "Detailed"};
        g_overlayRenderer.TriggerToast("Preset Changed", presetNames[nextPreset], 2.5f);
    }
    wasF8 = downF8;

    static bool wasF9 = false;
    bool downF9 = isDown(kcF9);
    if (downF9 && !wasF9) {
        g_enableOverlay = !g_enableOverlay;
        g_producer.SetOverlayEnabled(g_enableOverlay);
        g_overlayRenderer.TriggerToast("In-Game Overlay", g_enableOverlay ? "ENABLED" : "DISABLED", 2.0f);
        if (getenv("GNUMON_DEBUG") || getenv("GNUMON_OVERLAY")) {
            fprintf(stderr, "[gnumon-layer] In-game hotkey F9 pressed! In-Game HUD: %s\n",
                    g_enableOverlay ? "ON" : "OFF");
        }
    }
    wasF9 = downF9;

    static bool wasF10 = false;
    bool downF10 = isDown(kcF10);
    if (downF10 && !wasF10) {
        bool newRec = !g_producer.IsRecordingActive();
        g_producer.SetRecordingActive(newRec);
        g_overlayRenderer.TriggerToast(newRec ? "Benchmark Capture" : "Capture Saved",
                                       newRec ? "RECORDING STARTED" : "CSV BENCHMARK SAVED", 3.0f);
        if (getenv("GNUMON_DEBUG") || getenv("GNUMON_OVERLAY")) {
            fprintf(stderr, "[gnumon-layer] In-game hotkey F10 pressed! Capture: %s\n",
                    newRec ? "STARTING" : "STOPPED");
        }
    }
    wasF10 = downF10;
}

static VKAPI_ATTR VkResult VKAPI_CALL gnumon_vkQueuePresentKHR(
    VkQueue queue,
    const VkPresentInfoKHR* pPresentInfo)
{
    uint64_t presentStartNs = gnumon::common::Clock::GetTimestampNs();
    CheckInGameHotkeys(presentStartNs);
    static bool lastProducerOverlay = g_enableOverlay;
    bool currentProducerOverlay = g_producer.IsOverlayEnabled();
    if (currentProducerOverlay != lastProducerOverlay) {
        g_enableOverlay = currentProducerOverlay;
        lastProducerOverlay = currentProducerOverlay;
    }
    bool overlayActive = g_enableOverlay || g_overlayRenderer.HasActiveToast();
    bool isRec = g_producer.IsRecordingActive();
    bool overlayRendered = false;
    VkSemaphore overlaySignalSem = VK_NULL_HANDLE;

    // Render In-Game HUD into swapchain image before presenting
    if (overlayActive && pPresentInfo && pPresentInfo->swapchainCount > 0 && pPresentInfo->pSwapchains && pPresentInfo->pImageIndices) {
        VkSwapchainKHR sc = pPresentInfo->pSwapchains[0];
        uint32_t imgIdx = pPresentInfo->pImageIndices[0];

        VkDevice dev = VK_NULL_HANDLE;
        VkFormat fmt = VK_FORMAT_B8G8R8A8_UNORM;
        VkExtent2D extent{0, 0};
        std::vector<VkImage> scImages;
        {
            std::lock_guard<std::mutex> slock(g_swapchainMutex);
            auto it = g_swapchains.find(sc);
            if (it != g_swapchains.end()) {
                dev = it->second.device;
                fmt = it->second.format;
                extent = it->second.extent;
                scImages = it->second.images;
            }
        }

        if (dev) {
            DeviceDispatch disp{};
            {
                std::lock_guard<std::mutex> dlock(g_dispatchMutex);
                auto dit = g_deviceDispatch.find(GetDispatchKey(dev));
                if (dit != g_deviceDispatch.end()) {
                    disp = dit->second;
                }
            }

            VkQueue gfxQueue = disp.graphicsQueue ? disp.graphicsQueue : queue;
            uint32_t gfxQFam = disp.graphicsQueueFamilyIndex;
            uint32_t presentQFam = disp.defaultQueueFamilyIndex;
            {
                std::lock_guard<std::mutex> qlock(g_queueMutex);
                auto qit = g_queueFamilyMap.find(queue);
                if (qit != g_queueFamilyMap.end()) {
                    presentQFam = qit->second;
                }
            }

            if (!g_overlayRenderer.IsInitialized() || g_overlayRenderer.GetDevice() != dev) {
                g_overlayRenderer.Initialize(dev, disp.physicalDevice, gfxQueue, gfxQFam,
                                             disp.getProcAddr,
                                             g_getPhysicalDeviceMemoryProperties);
            }

            if (g_overlayRenderer.IsInitialized()) {
                // Ensure swapchain is registered in renderer
                if (!g_overlayRenderer.HasSwapchain(sc)) {
                    if (scImages.empty() && disp.getSwapchainImagesKHR) {
                        uint32_t count = 0;
                        if (disp.getSwapchainImagesKHR(dev, sc, &count, nullptr) == VK_SUCCESS && count > 0) {
                            scImages.resize(count);
                            disp.getSwapchainImagesKHR(dev, sc, &count, scImages.data());
                            std::lock_guard<std::mutex> slock(g_swapchainMutex);
                            g_swapchains[sc].images = scImages;
                        }
                    }
                    if (!scImages.empty() && extent.width > 0 && extent.height > 0) {
                        g_overlayRenderer.OnSwapchainCreated(sc, fmt, extent,
                                                             static_cast<uint32_t>(scImages.size()),
                                                             scImages.data());
                    }
                }

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

                gnumon::ipc::TelemetrySnapshot telemSnap{};
                bool hasTelem = g_producer.ReadTelemetry(telemSnap);

                if (g_overlayRenderer.HasSwapchain(sc)) {
                    if (g_overlayRenderer.RenderHud(gfxQueue, queue, gfxQFam, presentQFam,
                                                    sc, imgIdx,
                                                    presentFps, dispFps, lowFps,
                                                    ftMs, latMs, animErrMs, isRec,
                                                    g_hudCorner,
                                                    pPresentInfo->waitSemaphoreCount,
                                                    pPresentInfo->pWaitSemaphores,
                                                    &overlaySignalSem,
                                                    hasTelem ? &telemSnap : nullptr,
                                                    g_enableOverlay)) {
                        overlayRendered = true;
                    }
                }
            }
        }
    }

    VkPresentInfoKHR presentCopy = *pPresentInfo;
    if (overlayRendered && overlaySignalSem != VK_NULL_HANDLE) {
        presentCopy.waitSemaphoreCount = 1;
        presentCopy.pWaitSemaphores = &overlaySignalSem;
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
        result = nextFunc(queue, &presentCopy);
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
    event.psoCompileCount = g_psoCompileCount.exchange(0, std::memory_order_relaxed);
    event.psoCompileDurationNs = g_psoCompileDurationNs.exchange(0, std::memory_order_relaxed);

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
    if (IsProcessBlacklisted()) {
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
    if (std::strcmp(pName, "vkGetSwapchainImagesKHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkGetSwapchainImagesKHR);
    if (std::strcmp(pName, "vkGetDeviceQueue") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkGetDeviceQueue);
    if (std::strcmp(pName, "vkGetDeviceQueue2") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkGetDeviceQueue2);
    if (std::strcmp(pName, "vkCreateGraphicsPipelines") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkCreateGraphicsPipelines);
    if (std::strcmp(pName, "vkCreateComputePipelines") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkCreateComputePipelines);

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
    if (IsProcessBlacklisted()) {
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
    if (std::strcmp(pName, "vkGetSwapchainImagesKHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkGetSwapchainImagesKHR);
    if (std::strcmp(pName, "vkCreateGraphicsPipelines") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkCreateGraphicsPipelines);
    if (std::strcmp(pName, "vkCreateComputePipelines") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkCreateComputePipelines);
    if (std::strcmp(pName, "vkGetDeviceQueue") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkGetDeviceQueue);
    if (std::strcmp(pName, "vkGetDeviceQueue2") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(gnumon_vkGetDeviceQueue2);

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
