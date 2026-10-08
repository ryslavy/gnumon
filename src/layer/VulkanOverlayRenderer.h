#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <cstring>
#include <cmath>
#include <cstdio>
#include "Font8x8.h"

namespace gnumon::layer {

class VulkanOverlayRenderer {
public:
    static constexpr uint32_t HUD_WIDTH = 440;
    static constexpr uint32_t HUD_HEIGHT = 80;

    VulkanOverlayRenderer() = default;
    ~VulkanOverlayRenderer() {
        Cleanup();
    }

    bool Initialize(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
                    PFN_vkGetDeviceProcAddr gdpa, PFN_vkGetInstanceProcAddr gipa, VkInstance instance)
    {
        if (initialized_) return true;
        if (!device || !gdpa) return false;

        device_ = device;

        createCommandPool_ = (PFN_vkCreateCommandPool)gdpa(device, "vkCreateCommandPool");
        destroyCommandPool_ = (PFN_vkDestroyCommandPool)gdpa(device, "vkDestroyCommandPool");
        allocateCommandBuffers_ = (PFN_vkAllocateCommandBuffers)gdpa(device, "vkAllocateCommandBuffers");
        freeCommandBuffers_ = (PFN_vkFreeCommandBuffers)gdpa(device, "vkFreeCommandBuffers");
        beginCommandBuffer_ = (PFN_vkBeginCommandBuffer)gdpa(device, "vkBeginCommandBuffer");
        endCommandBuffer_ = (PFN_vkEndCommandBuffer)gdpa(device, "vkEndCommandBuffer");
        cmdPipelineBarrier_ = (PFN_vkCmdPipelineBarrier)gdpa(device, "vkCmdPipelineBarrier");
        cmdCopyBufferToImage_ = (PFN_vkCmdCopyBufferToImage)gdpa(device, "vkCmdCopyBufferToImage");
        createBuffer_ = (PFN_vkCreateBuffer)gdpa(device, "vkCreateBuffer");
        destroyBuffer_ = (PFN_vkDestroyBuffer)gdpa(device, "vkDestroyBuffer");
        getBufferMemoryRequirements_ = (PFN_vkGetBufferMemoryRequirements)gdpa(device, "vkGetBufferMemoryRequirements");
        allocateMemory_ = (PFN_vkAllocateMemory)gdpa(device, "vkAllocateMemory");
        freeMemory_ = (PFN_vkFreeMemory)gdpa(device, "vkFreeMemory");
        bindBufferMemory_ = (PFN_vkBindBufferMemory)gdpa(device, "vkBindBufferMemory");
        mapMemory_ = (PFN_vkMapMemory)gdpa(device, "vkMapMemory");
        unmapMemory_ = (PFN_vkUnmapMemory)gdpa(device, "vkUnmapMemory");
        queueSubmit_ = (PFN_vkQueueSubmit)gdpa(device, "vkQueueSubmit");
        queueWaitIdle_ = (PFN_vkQueueWaitIdle)gdpa(device, "vkQueueWaitIdle");
        deviceWaitIdle_ = (PFN_vkDeviceWaitIdle)gdpa(device, "vkDeviceWaitIdle");

        PFN_vkGetPhysicalDeviceMemoryProperties getMemProps = nullptr;
        if (gipa) {
            if (instance) {
                getMemProps = (PFN_vkGetPhysicalDeviceMemoryProperties)gipa(instance, "vkGetPhysicalDeviceMemoryProperties");
            }
            if (!getMemProps) {
                getMemProps = (PFN_vkGetPhysicalDeviceMemoryProperties)gipa(VK_NULL_HANDLE, "vkGetPhysicalDeviceMemoryProperties");
            }
        }

        if (!createCommandPool_ || !allocateCommandBuffers_ || !createBuffer_ ||
            !allocateMemory_ || !mapMemory_ || !getMemProps || !queueSubmit_ || !queueWaitIdle_) {
            return false;
        }

        // 1. Create Command Pool
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilyIndex;
        if (createCommandPool_(device, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
            return false;
        }

        // 2. Allocate Command Buffer
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool_;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        if (allocateCommandBuffers_(device, &allocInfo, &commandBuffer_) != VK_SUCCESS) {
            return false;
        }

        // 3. Create Host-Visible Staging Buffer
        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = HUD_WIDTH * HUD_HEIGHT * 4;
        bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (createBuffer_(device, &bufInfo, nullptr, &stagingBuffer_) != VK_SUCCESS) {
            return false;
        }

        VkMemoryRequirements memReq{};
        getBufferMemoryRequirements_(device, stagingBuffer_, &memReq);

        VkPhysicalDeviceMemoryProperties memProperties{};
        getMemProps(physicalDevice, &memProperties);

        uint32_t memTypeIndex = 0;
        bool foundMemType = false;
        uint32_t reqFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            if ((memReq.memoryTypeBits & (1 << i)) &&
                ((memProperties.memoryTypes[i].propertyFlags & reqFlags) == reqFlags)) {
                memTypeIndex = i;
                foundMemType = true;
                break;
            }
        }
        if (!foundMemType) {
            return false;
        }

        VkMemoryAllocateInfo memAlloc{};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memAlloc.allocationSize = memReq.size;
        memAlloc.memoryTypeIndex = memTypeIndex;
        if (allocateMemory_(device, &memAlloc, nullptr, &stagingMemory_) != VK_SUCCESS) {
            return false;
        }

        if (bindBufferMemory_(device, stagingBuffer_, stagingMemory_, 0) != VK_SUCCESS) {
            return false;
        }

        if (mapMemory_(device, stagingMemory_, 0, bufInfo.size, 0, reinterpret_cast<void**>(&mappedPixels_)) != VK_SUCCESS) {
            return false;
        }

        initialized_ = true;
        return true;
    }

    void Cleanup() {
        if (!initialized_) return;
        if (device_) {
            if (deviceWaitIdle_) deviceWaitIdle_(device_);
            if (mappedPixels_ && unmapMemory_) unmapMemory_(device_, stagingMemory_);
            if (stagingBuffer_ && destroyBuffer_) destroyBuffer_(device_, stagingBuffer_, nullptr);
            if (stagingMemory_ && freeMemory_) freeMemory_(device_, stagingMemory_, nullptr);
            if (commandPool_ && destroyCommandPool_) destroyCommandPool_(device_, commandPool_, nullptr);
        }
        mappedPixels_ = nullptr;
        stagingBuffer_ = VK_NULL_HANDLE;
        stagingMemory_ = VK_NULL_HANDLE;
        commandPool_ = VK_NULL_HANDLE;
        commandBuffer_ = VK_NULL_HANDLE;
        initialized_ = false;
    }

    bool IsInitialized() const noexcept {
        return initialized_;
    }

    void RenderHud(VkQueue queue, VkImage image, VkFormat format,
                   double presentFps, double displayedFps, double fps1PercentLow,
                   double frameTimeMs, double latencyMs, double animErrorMs,
                   bool isRecording, int corner = 0, uint32_t swapchainWidth = 1920, uint32_t swapchainHeight = 1080)
    {
        if (!initialized_ || !mappedPixels_ || !image || !queue) return;

        bool isBgra = (format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB ||
                       format == VK_FORMAT_B8G8R8A8_SNORM);

        auto col = [isBgra](uint8_t r, uint8_t g, uint8_t b, uint8_t a) -> uint32_t {
            if (isBgra) {
                return (static_cast<uint32_t>(a) << 24) |
                       (static_cast<uint32_t>(r) << 16) |
                       (static_cast<uint32_t>(g) << 8)  |
                       (static_cast<uint32_t>(b));
            } else {
                return (static_cast<uint32_t>(a) << 24) |
                       (static_cast<uint32_t>(b) << 16) |
                       (static_cast<uint32_t>(g) << 8)  |
                       (static_cast<uint32_t>(r));
            }
        };

        uint32_t bg = col(12, 16, 24, 215);
        uint32_t borderCol = col(0, 188, 212, 230);
        uint32_t titleCol = col(0, 229, 255, 255);
        uint32_t recCol = col(244, 67, 54, 255);
        uint32_t textWhite = col(240, 245, 250, 255);
        uint32_t greenCol = col(129, 199, 132, 255);
        uint32_t amberCol = col(255, 183, 77, 255);
        uint32_t coralCol = col(255, 138, 101, 255);
        uint32_t subCol = col(144, 164, 174, 255);

        // Clear HUD with background
        for (uint32_t i = 0; i < HUD_WIDTH * HUD_HEIGHT; ++i) {
            mappedPixels_[i] = bg;
        }

        // Draw HUD border
        for (uint32_t x = 0; x < HUD_WIDTH; ++x) {
            mappedPixels_[x] = borderCol;
            mappedPixels_[(HUD_HEIGHT - 1) * HUD_WIDTH + x] = borderCol;
        }
        for (uint32_t y = 0; y < HUD_HEIGHT; ++y) {
            mappedPixels_[y * HUD_WIDTH] = borderCol;
            mappedPixels_[y * HUD_WIDTH + (HUD_WIDTH - 1)] = borderCol;
        }

        auto drawChar = [&](int x, int y, char c, uint32_t color) {
            auto uc = static_cast<unsigned char>(c);
            if (uc >= 128) return;
            const uint8_t* glyph = s_font8x8[uc];
            for (int r = 0; r < 8; ++r) {
                int py = y + r;
                if (py < 0 || py >= static_cast<int>(HUD_HEIGHT)) continue;
                uint8_t bits = glyph[r];
                for (int cl = 0; cl < 8; ++cl) {
                    int px = x + cl;
                    if (px < 0 || px >= static_cast<int>(HUD_WIDTH)) continue;
                    if (bits & (1 << cl)) {
                        mappedPixels_[py * HUD_WIDTH + px] = color;
                    }
                }
            }
        };

        auto drawString = [&](int x, int y, const std::string& str, uint32_t color) {
            int curX = x;
            for (char ch : str) {
                drawChar(curX, y, ch, color);
                curX += 8;
            }
        };

        char buf[128];

        // Line 1: Header
        drawString(10, 8, "GNUMON PRESENTMON [LINUX]", titleCol);
        if (isRecording) {
            drawString(340, 8, "[* REC]", recCol);
        }

        // Line 2: FPS Metrics (Present / Display / 1% Low)
        std::snprintf(buf, sizeof(buf), "PRES: %4.0f FPS", presentFps);
        drawString(10, 24, buf, textWhite);

        std::snprintf(buf, sizeof(buf), "DISP: %4.0f FPS", (displayedFps > 0.0) ? displayedFps : presentFps);
        drawString(150, 24, buf, titleCol);

        double lowVal = (fps1PercentLow > 0.0) ? fps1PercentLow : (presentFps * 0.7);
        std::snprintf(buf, sizeof(buf), "1%% LOW: %4.0f", lowVal);
        drawString(290, 24, buf, coralCol);

        // Line 3: Frame Time / Latency / Animation Error
        std::snprintf(buf, sizeof(buf), "FT: %4.1f ms", frameTimeMs);
        drawString(10, 42, buf, greenCol);

        std::snprintf(buf, sizeof(buf), "LAT: %4.1f ms", (latencyMs > 0.0) ? latencyMs : frameTimeMs);
        drawString(150, 42, buf, amberCol);

        std::snprintf(buf, sizeof(buf), "ANIM ERR: %4.2f ms", animErrorMs);
        drawString(290, 42, buf, coralCol);

        // Line 4: Hints
        drawString(10, 60, "F11: Toggle Overlay   |   F10: Toggle Capture", subCol);

        // Record transfer command buffer
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (beginCommandBuffer_(commandBuffer_, &beginInfo) != VK_SUCCESS) {
            return;
        }

        // 1. Transition swapchain image from PRESENT_SRC to TRANSFER_DST
        VkImageMemoryBarrier barrier1{};
        barrier1.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier1.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier1.image = image;
        barrier1.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier1.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier1.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        cmdPipelineBarrier_(commandBuffer_,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier1);

        // 2. Copy staging buffer to swapchain image
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = HUD_WIDTH;
        region.bufferImageHeight = HUD_HEIGHT;
        int32_t ox = 24;
        int32_t oy = 24;
        if (corner == 1) { // Top-Right
            if (swapchainWidth > HUD_WIDTH + 48) ox = static_cast<int32_t>(swapchainWidth - HUD_WIDTH - 24);
        } else if (corner == 2) { // Bottom-Left
            if (swapchainHeight > HUD_HEIGHT + 48) oy = static_cast<int32_t>(swapchainHeight - HUD_HEIGHT - 24);
        } else if (corner == 3) { // Bottom-Right
            if (swapchainWidth > HUD_WIDTH + 48) ox = static_cast<int32_t>(swapchainWidth - HUD_WIDTH - 24);
            if (swapchainHeight > HUD_HEIGHT + 48) oy = static_cast<int32_t>(swapchainHeight - HUD_HEIGHT - 24);
        }
        region.imageOffset = {ox, oy, 0};
        region.imageExtent = {HUD_WIDTH, HUD_HEIGHT, 1};

        cmdCopyBufferToImage_(commandBuffer_, stagingBuffer_, image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // 3. Transition back from TRANSFER_DST to PRESENT_SRC
        VkImageMemoryBarrier barrier2{};
        barrier2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier2.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier2.image = image;
        barrier2.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier2.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

        cmdPipelineBarrier_(commandBuffer_,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier2);

        endCommandBuffer_(commandBuffer_);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer_;
        queueSubmit_(queue, 1, &submitInfo, VK_NULL_HANDLE);
        if (queueWaitIdle_) queueWaitIdle_(queue);
    }

private:
    bool initialized_ = false;
    VkDevice device_ = VK_NULL_HANDLE;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkBuffer stagingBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory_ = VK_NULL_HANDLE;
    uint32_t* mappedPixels_ = nullptr;

    PFN_vkCreateCommandPool createCommandPool_ = nullptr;
    PFN_vkDestroyCommandPool destroyCommandPool_ = nullptr;
    PFN_vkAllocateCommandBuffers allocateCommandBuffers_ = nullptr;
    PFN_vkFreeCommandBuffers freeCommandBuffers_ = nullptr;
    PFN_vkBeginCommandBuffer beginCommandBuffer_ = nullptr;
    PFN_vkEndCommandBuffer endCommandBuffer_ = nullptr;
    PFN_vkCmdPipelineBarrier cmdPipelineBarrier_ = nullptr;
    PFN_vkCmdCopyBufferToImage cmdCopyBufferToImage_ = nullptr;
    PFN_vkCreateBuffer createBuffer_ = nullptr;
    PFN_vkDestroyBuffer destroyBuffer_ = nullptr;
    PFN_vkGetBufferMemoryRequirements getBufferMemoryRequirements_ = nullptr;
    PFN_vkAllocateMemory allocateMemory_ = nullptr;
    PFN_vkFreeMemory freeMemory_ = nullptr;
    PFN_vkBindBufferMemory bindBufferMemory_ = nullptr;
    PFN_vkMapMemory mapMemory_ = nullptr;
    PFN_vkUnmapMemory unmapMemory_ = nullptr;
    PFN_vkQueueSubmit queueSubmit_ = nullptr;
    PFN_vkQueueWaitIdle queueWaitIdle_ = nullptr;
    PFN_vkDeviceWaitIdle deviceWaitIdle_ = nullptr;
};

} // namespace gnumon::layer
