#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <unordered_map>
#include <mutex>
#include <deque>
#if !defined(_WIN32)
#include <dlfcn.h>
#endif
#include "font_atlas.hpp"
#include "vk_overlay_shaders.hpp"
#include "HudVertexGenerator.h"
#include "../ipc/FrameRingBuffer.h"

namespace gnumon::layer {

struct OverlayPushConstants {
    float screen_w;
    float screen_h;
};

class VulkanOverlayRenderer : public HudVertexGenerator {
public:
    VulkanOverlayRenderer() = default;
    ~VulkanOverlayRenderer() {
        Cleanup();
    }

    bool Initialize(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue queue, uint32_t queueFamilyIndex,
                    PFN_vkGetDeviceProcAddr gdpa, PFN_vkGetPhysicalDeviceMemoryProperties getMemProps)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_ && device_ == device) return true;
        if (initialized_) CleanupLocked();
        if (!device || !gdpa) return false;

        device_ = device;
        physDevice_ = physicalDevice;
        queue_ = queue;
        queueFamily_ = queueFamilyIndex;
        gdpa_ = gdpa;

        LoadDeviceProcs(device, gdpa, getMemProps);

        if (!createCommandPool_ || !allocateCommandBuffers_ || !createBuffer_ ||
            !allocateMemory_ || !mapMemory_ || !getMemProps_ || !queueSubmit_ ||
            !createRenderPass_ || !createGraphicsPipelines_) {
            return false;
        }

        // 1. Create primary Command Pool
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueFamilyIndex;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (createCommandPool_(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
            return false;
        }

        // 2. Create Font Atlas Texture & Sampler
        CreateFontTexture();

        initialized_ = true;
        return true;
    }

    bool IsInitialized() const noexcept {
        return initialized_;
    }

    VkDevice GetDevice() const noexcept {
        return device_;
    }

    void OnSwapchainCreated(VkSwapchainKHR swapchain, VkFormat format, VkExtent2D extent,
                            uint32_t imageCount, const VkImage* images)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ || !device_) return;

        DestroySwapchainLocked(swapchain);

        if (extent.width == 0 || extent.height == 0 || imageCount == 0 || !images) {
            return;
        }

        CreateRenderPass(format);
        CreatePipeline();

        SwapchainData data{};
        data.extent = extent;
        data.format = format;
        data.images.assign(images, images + imageCount);
        data.views.resize(imageCount, VK_NULL_HANDLE);
        data.framebuffers.resize(imageCount, VK_NULL_HANDLE);
        data.cmdBuffers.resize(imageCount, VK_NULL_HANDLE);
        data.fences.resize(imageCount, VK_NULL_HANDLE);
        data.fenceSubmitted.resize(imageCount, false);
        data.semaphores.resize(imageCount, VK_NULL_HANDLE);
        data.vertexBuffers.resize(imageCount, VK_NULL_HANDLE);
        data.vertexMemories.resize(imageCount, VK_NULL_HANDLE);
        data.vertexMapped.resize(imageCount, nullptr);

        VkCommandBufferAllocateInfo cmdAlloc{};
        cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAlloc.commandPool = commandPool_;
        cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAlloc.commandBufferCount = imageCount;
        allocateCommandBuffers_(device_, &cmdAlloc, data.cmdBuffers.data());

        for (uint32_t i = 0; i < imageCount; ++i) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = images[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = format;
            viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            createImageView_(device_, &viewInfo, nullptr, &data.views[i]);

            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = renderPass_;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &data.views[i];
            fbInfo.width = extent.width;
            fbInfo.height = extent.height;
            fbInfo.layers = 1;
            createFramebuffer_(device_, &fbInfo, nullptr, &data.framebuffers[i]);

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            createFence_(device_, &fenceInfo, nullptr, &data.fences[i]);

            VkSemaphoreCreateInfo semInfo{};
            semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            createSemaphore_(device_, &semInfo, nullptr, &data.semaphores[i]);

            // Host-visible vertex buffer per frame
            const size_t vbSize = 16384 * sizeof(OverlayVertex);
            VkBufferCreateInfo bufInfo{};
            bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufInfo.size = vbSize;
            bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createBuffer_(device_, &bufInfo, nullptr, &data.vertexBuffers[i]);

            VkMemoryRequirements memReq{};
            getBufferMemoryRequirements_(device_, data.vertexBuffers[i], &memReq);

            VkMemoryAllocateInfo memAlloc{};
            memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            memAlloc.allocationSize = memReq.size;
            memAlloc.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            allocateMemory_(device_, &memAlloc, nullptr, &data.vertexMemories[i]);
            bindBufferMemory_(device_, data.vertexBuffers[i], data.vertexMemories[i], 0);

            mapMemory_(device_, data.vertexMemories[i], 0, vbSize, 0,
                       reinterpret_cast<void**>(&data.vertexMapped[i]));
        }

        swapchains_[swapchain] = std::move(data);
    }

    void OnSwapchainDestroyed(VkSwapchainKHR swapchain) {
        std::lock_guard<std::mutex> lock(mutex_);
        DestroySwapchainLocked(swapchain);
    }

    bool HasSwapchain(VkSwapchainKHR swapchain) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return swapchains_.find(swapchain) != swapchains_.end();
    }

    void SetPreset(int preset) {
        std::lock_guard<std::mutex> lock(mutex_);
        hudPreset_ = std::clamp(preset, 0, 2);
    }

    int GetPreset() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return hudPreset_;
    }

    bool HasActiveToast() const {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t nowNs = gnumon::common::Clock::GetTimestampNs();
        return (nowNs < toastExpiryNs_ && !toastTitle_.empty());
    }

    void TriggerToast(const std::string& title, const std::string& message, float durationSec = 3.0f) {
        std::lock_guard<std::mutex> lock(mutex_);
        toastTitle_ = title;
        toastMessage_ = message;
        uint64_t nowNs = gnumon::common::Clock::GetTimestampNs();
        toastExpiryNs_ = nowNs + static_cast<uint64_t>(durationSec * 1'000'000'000.0f);
    }

    bool RenderHud(VkQueue graphicsQueue, VkQueue presentQueue,
                   uint32_t graphicsQueueFamily, uint32_t presentQueueFamily,
                   VkSwapchainKHR swapchain, uint32_t imageIndex,
                   double presentFps, double displayedFps, double fps1PercentLow,
                   double frameTimeMs, double latencyMs, double animErrorMs,
                   bool isRecording, int corner,
                   uint32_t waitSemCount, const VkSemaphore* pWaitSems,
                   VkSemaphore* outSignalSem,
                   const ipc::TelemetrySnapshot* telem = nullptr,
                   bool hudVisible = true)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ || !device_) return false;

        auto it = swapchains_.find(swapchain);
        if (it == swapchains_.end() || imageIndex >= it->second.cmdBuffers.size()) {
            return false;
        }

        SwapchainData& sd = it->second;
        uint32_t sw = sd.extent.width;
        uint32_t sh = sd.extent.height;
        if (sw == 0 || sh == 0) return false;

        if (frameTimeMs > 0.0) {
            frametimes_.push_back(static_cast<float>(frameTimeMs));
            if (frametimes_.size() > 128) {
                frametimes_.pop_front();
            }
        }

        std::vector<OverlayVertex> verts;
        GenerateHudVertices(verts, sw, sh, corner,
                            presentFps, displayedFps, fps1PercentLow,
                            frameTimeMs, latencyMs, animErrorMs, isRecording, telem, hudVisible);

        if (verts.empty() || imageIndex >= sd.vertexMapped.size() || !sd.vertexMapped[imageIndex]) {
            return false;
        }

        // Wait on fence for this image before overwriting its vertex buffer
        if (imageIndex < sd.fences.size() && sd.fenceSubmitted[imageIndex] && sd.fences[imageIndex] != VK_NULL_HANDLE) {
            waitForFences_(device_, 1, &sd.fences[imageIndex], VK_TRUE, 1000000000ULL);
            resetFences_(device_, 1, &sd.fences[imageIndex]);
        }

        size_t copySize = std::min(verts.size() * sizeof(OverlayVertex),
                                   static_cast<size_t>(16384 * sizeof(OverlayVertex)));
        memcpy(sd.vertexMapped[imageIndex], verts.data(), copySize);

        if (flushMappedMemoryRanges_) {
            VkMappedMemoryRange range{};
            range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory = sd.vertexMemories[imageIndex];
            range.offset = 0;
            range.size = VK_WHOLE_SIZE;
            flushMappedMemoryRanges_(device_, 1, &range);
        }

        VkCommandBuffer cmd = sd.cmdBuffers[imageIndex];
        if (resetCommandBuffer_) {
            resetCommandBuffer_(cmd, 0);
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        beginCommandBuffer_(cmd, &beginInfo);

        // Transition swapchain image to COLOR_ATTACHMENT_OPTIMAL if not already in render pass
        VkImageMemoryBarrier imb{};
        imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        imb.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        imb.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        imb.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        imb.image = sd.images[imageIndex];
        imb.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        cmdPipelineBarrier_(cmd, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
                            0, 0, nullptr, 0, nullptr, 1, &imb);

        VkRenderPassBeginInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass = renderPass_;
        rpInfo.framebuffer = sd.framebuffers[imageIndex];
        rpInfo.renderArea.offset = {0, 0};
        rpInfo.renderArea.extent = sd.extent;

        cmdBeginRenderPass_(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp{0.0f, 0.0f, static_cast<float>(sw), static_cast<float>(sh), 0.0f, 1.0f};
        VkRect2D sc{{0, 0}, sd.extent};
        cmdBindPipeline_(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        cmdSetViewport_(cmd, 0, 1, &vp);
        cmdSetScissor_(cmd, 0, 1, &sc);

        OverlayPushConstants pc{static_cast<float>(sw), static_cast<float>(sh)};
        cmdPushConstants_(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(OverlayPushConstants), &pc);
        cmdBindDescriptorSets_(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descSet_, 0, nullptr);

        VkDeviceSize offset = 0;
        cmdBindVertexBuffers_(cmd, 0, 1, &sd.vertexBuffers[imageIndex], &offset);
        cmdDraw_(cmd, static_cast<uint32_t>(verts.size()), 1, 0, 0);

        cmdEndRenderPass_(cmd);
        endCommandBuffer_(cmd);

        // Submit to graphics queue, waiting on game frame semaphores and signaling overlay semaphore
        std::vector<VkPipelineStageFlags> waitStages(waitSemCount > 0 ? waitSemCount : 1,
                                                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        if (waitSemCount > 0 && pWaitSems) {
            submitInfo.waitSemaphoreCount = waitSemCount;
            submitInfo.pWaitSemaphores = pWaitSems;
            submitInfo.pWaitDstStageMask = waitStages.data();
        }
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        if (imageIndex < sd.semaphores.size()) {
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &sd.semaphores[imageIndex];
        }

        VkFence fenceToSignal = (imageIndex < sd.fences.size()) ? sd.fences[imageIndex] : VK_NULL_HANDLE;
        VkResult subRes = queueSubmit_(graphicsQueue ? graphicsQueue : queue_, 1, &submitInfo, fenceToSignal);
        if (subRes != VK_SUCCESS) {
            return false;
        }

        sd.fenceSubmitted[imageIndex] = true;
        if (outSignalSem && imageIndex < sd.semaphores.size()) {
            *outSignalSem = sd.semaphores[imageIndex];
        }
        return true;
    }

    void Cleanup() {
        std::lock_guard<std::mutex> lock(mutex_);
        CleanupLocked();
    }

private:
    struct SwapchainData {
        VkExtent2D extent{0, 0};
        VkFormat format = VK_FORMAT_UNDEFINED;
        std::vector<VkImage> images;
        std::vector<VkImageView> views;
        std::vector<VkFramebuffer> framebuffers;
        std::vector<VkCommandBuffer> cmdBuffers;
        std::vector<VkFence> fences;
        std::vector<bool> fenceSubmitted;
        std::vector<VkSemaphore> semaphores;
        std::vector<VkBuffer> vertexBuffers;
        std::vector<VkDeviceMemory> vertexMemories;
        std::vector<OverlayVertex*> vertexMapped;
    };

    void LoadDeviceProcs(VkDevice dev, PFN_vkGetDeviceProcAddr gdpa, PFN_vkGetPhysicalDeviceMemoryProperties getMemProps) {
#define LOAD_PROC(member, name) member = reinterpret_cast<PFN_vk##name>(gdpa(dev, "vk" #name))
        LOAD_PROC(createCommandPool_, CreateCommandPool);
        LOAD_PROC(destroyCommandPool_, DestroyCommandPool);
        LOAD_PROC(createBuffer_, CreateBuffer);
        LOAD_PROC(destroyBuffer_, DestroyBuffer);
        LOAD_PROC(getBufferMemoryRequirements_, GetBufferMemoryRequirements);
        LOAD_PROC(allocateMemory_, AllocateMemory);
        LOAD_PROC(freeMemory_, FreeMemory);
        LOAD_PROC(bindBufferMemory_, BindBufferMemory);
        LOAD_PROC(mapMemory_, MapMemory);
        LOAD_PROC(unmapMemory_, UnmapMemory);
        LOAD_PROC(createImage_, CreateImage);
        LOAD_PROC(destroyImage_, DestroyImage);
        LOAD_PROC(getImageMemoryRequirements_, GetImageMemoryRequirements);
        LOAD_PROC(bindImageMemory_, BindImageMemory);
        LOAD_PROC(createImageView_, CreateImageView);
        LOAD_PROC(destroyImageView_, DestroyImageView);
        LOAD_PROC(createSampler_, CreateSampler);
        LOAD_PROC(destroySampler_, DestroySampler);
        LOAD_PROC(createDescriptorSetLayout_, CreateDescriptorSetLayout);
        LOAD_PROC(destroyDescriptorSetLayout_, DestroyDescriptorSetLayout);
        LOAD_PROC(createDescriptorPool_, CreateDescriptorPool);
        LOAD_PROC(destroyDescriptorPool_, DestroyDescriptorPool);
        LOAD_PROC(allocateDescriptorSets_, AllocateDescriptorSets);
        LOAD_PROC(updateDescriptorSets_, UpdateDescriptorSets);
        LOAD_PROC(createRenderPass_, CreateRenderPass);
        LOAD_PROC(destroyRenderPass_, DestroyRenderPass);
        LOAD_PROC(createShaderModule_, CreateShaderModule);
        LOAD_PROC(destroyShaderModule_, DestroyShaderModule);
        LOAD_PROC(createPipelineLayout_, CreatePipelineLayout);
        LOAD_PROC(destroyPipelineLayout_, DestroyPipelineLayout);
        LOAD_PROC(createGraphicsPipelines_, CreateGraphicsPipelines);
        LOAD_PROC(destroyPipeline_, DestroyPipeline);
        LOAD_PROC(createFramebuffer_, CreateFramebuffer);
        LOAD_PROC(destroyFramebuffer_, DestroyFramebuffer);
        LOAD_PROC(createFence_, CreateFence);
        LOAD_PROC(destroyFence_, DestroyFence);
        LOAD_PROC(waitForFences_, WaitForFences);
        LOAD_PROC(resetFences_, ResetFences);
        LOAD_PROC(allocateCommandBuffers_, AllocateCommandBuffers);
        LOAD_PROC(freeCommandBuffers_, FreeCommandBuffers);
        LOAD_PROC(resetCommandBuffer_, ResetCommandBuffer);
        LOAD_PROC(beginCommandBuffer_, BeginCommandBuffer);
        LOAD_PROC(endCommandBuffer_, EndCommandBuffer);
        LOAD_PROC(cmdPipelineBarrier_, CmdPipelineBarrier);
        LOAD_PROC(cmdCopyBufferToImage_, CmdCopyBufferToImage);
        LOAD_PROC(cmdBeginRenderPass_, CmdBeginRenderPass);
        LOAD_PROC(cmdEndRenderPass_, CmdEndRenderPass);
        LOAD_PROC(cmdBindPipeline_, CmdBindPipeline);
        LOAD_PROC(cmdSetViewport_, CmdSetViewport);
        LOAD_PROC(cmdSetScissor_, CmdSetScissor);
        LOAD_PROC(cmdPushConstants_, CmdPushConstants);
        LOAD_PROC(cmdBindDescriptorSets_, CmdBindDescriptorSets);
        LOAD_PROC(cmdBindVertexBuffers_, CmdBindVertexBuffers);
        LOAD_PROC(cmdDraw_, CmdDraw);
        LOAD_PROC(createSemaphore_, CreateSemaphore);
        LOAD_PROC(destroySemaphore_, DestroySemaphore);
        LOAD_PROC(queueSubmit_, QueueSubmit);
        LOAD_PROC(queueWaitIdle_, QueueWaitIdle);
        LOAD_PROC(deviceWaitIdle_, DeviceWaitIdle);
        LOAD_PROC(flushMappedMemoryRanges_, FlushMappedMemoryRanges);
#undef LOAD_PROC

        getMemProps_ = getMemProps;
        if (!getMemProps_) {
#if !defined(_WIN32)
            getMemProps_ = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
                dlsym(RTLD_DEFAULT, "vkGetPhysicalDeviceMemoryProperties"));
#endif
        }
    }

    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        if (!getMemProps_ || !physDevice_) return 0;
        VkPhysicalDeviceMemoryProperties memProperties{};
        getMemProps_(physDevice_, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            if ((typeFilter & (1 << i)) &&
                ((memProperties.memoryTypes[i].propertyFlags & properties) == properties)) {
                return i;
            }
        }
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            if (typeFilter & (1 << i)) {
                return i;
            }
        }
        return 0;
    }

    void CreateFontTexture() {
        const uint32_t tex_w = FONT_TEX_W;
        const uint32_t tex_h = FONT_TEX_H;

        VkBuffer stgBuf = VK_NULL_HANDLE;
        VkDeviceMemory stgMem = VK_NULL_HANDLE;

        VkBufferCreateInfo stgInfo{};
        stgInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stgInfo.size = sizeof(font_atlas_bitmap);
        stgInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        createBuffer_(device_, &stgInfo, nullptr, &stgBuf);

        VkMemoryRequirements stgReq{};
        getBufferMemoryRequirements_(device_, stgBuf, &stgReq);

        VkMemoryAllocateInfo stgAlloc{};
        stgAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        stgAlloc.allocationSize = stgReq.size;
        stgAlloc.memoryTypeIndex = FindMemoryType(stgReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        allocateMemory_(device_, &stgAlloc, nullptr, &stgMem);
        bindBufferMemory_(device_, stgBuf, stgMem, 0);

        void* data = nullptr;
        mapMemory_(device_, stgMem, 0, sizeof(font_atlas_bitmap), 0, &data);
        memcpy(data, font_atlas_bitmap, sizeof(font_atlas_bitmap));
        unmapMemory_(device_, stgMem);

        VkImageCreateInfo imgInfo{};
        imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType = VK_IMAGE_TYPE_2D;
        imgInfo.format = VK_FORMAT_R8_UNORM;
        imgInfo.extent = {tex_w, tex_h, 1};
        imgInfo.mipLevels = 1;
        imgInfo.arrayLayers = 1;
        imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        createImage_(device_, &imgInfo, nullptr, &fontImage_);

        VkMemoryRequirements imgReq{};
        getImageMemoryRequirements_(device_, fontImage_, &imgReq);

        VkMemoryAllocateInfo imgAlloc{};
        imgAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        imgAlloc.allocationSize = imgReq.size;
        imgAlloc.memoryTypeIndex = FindMemoryType(imgReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        allocateMemory_(device_, &imgAlloc, nullptr, &fontMemory_);
        bindImageMemory_(device_, fontImage_, fontMemory_, 0);

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo cmdAlloc{};
        cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAlloc.commandPool = commandPool_;
        cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAlloc.commandBufferCount = 1;
        allocateCommandBuffers_(device_, &cmdAlloc, &cmd);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        beginCommandBuffer_(cmd, &beginInfo);

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = fontImage_;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        cmdPipelineBarrier_(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {tex_w, tex_h, 1};
        cmdCopyBufferToImage_(cmd, stgBuf, fontImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        cmdPipelineBarrier_(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            0, 0, nullptr, 0, nullptr, 1, &barrier);

        endCommandBuffer_(cmd);

        VkFence uploadFence = VK_NULL_HANDLE;
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        createFence_(device_, &fenceInfo, nullptr, &uploadFence);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        if (queueSubmit_(queue_, 1, &submitInfo, uploadFence) == VK_SUCCESS) {
            waitForFences_(device_, 1, &uploadFence, VK_TRUE, 1000000000ULL);
        }
        destroyFence_(device_, uploadFence, nullptr);
        freeCommandBuffers_(device_, commandPool_, 1, &cmd);

        destroyBuffer_(device_, stgBuf, nullptr);
        freeMemory_(device_, stgMem, nullptr);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = fontImage_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8_UNORM;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        createImageView_(device_, &viewInfo, nullptr, &fontView_);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        createSampler_(device_, &samplerInfo, nullptr, &fontSampler_);

        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        createDescriptorSetLayout_(device_, &layoutInfo, nullptr, &descLayout_);

        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo descPoolInfo{};
        descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descPoolInfo.maxSets = 1;
        descPoolInfo.poolSizeCount = 1;
        descPoolInfo.pPoolSizes = &poolSize;
        createDescriptorPool_(device_, &descPoolInfo, nullptr, &descPool_);

        VkDescriptorSetAllocateInfo setAlloc{};
        setAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setAlloc.descriptorPool = descPool_;
        setAlloc.descriptorSetCount = 1;
        setAlloc.pSetLayouts = &descLayout_;
        allocateDescriptorSets_(device_, &setAlloc, &descSet_);

        VkDescriptorImageInfo descImgInfo{};
        descImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descImgInfo.imageView = fontView_;
        descImgInfo.sampler = fontSampler_;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descSet_;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &descImgInfo;
        updateDescriptorSets_(device_, 1, &write, 0, nullptr);
    }

    void CreateRenderPass(VkFormat format) {
        if (renderPass_ != VK_NULL_HANDLE && format_ == format) return;

        if (renderPass_ != VK_NULL_HANDLE) {
            destroyRenderPass_(device_, renderPass_, nullptr);
            renderPass_ = VK_NULL_HANDLE;
        }
        if (pipeline_ != VK_NULL_HANDLE) {
            destroyPipeline_(device_, pipeline_, nullptr);
            destroyPipelineLayout_(device_, pipelineLayout_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
            pipelineLayout_ = VK_NULL_HANDLE;
        }
        format_ = format;

        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = format;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Preserve underlying game render!
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = 0;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = 0;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 2;
        rpInfo.pDependencies = dependencies;
        createRenderPass_(device_, &rpInfo, nullptr, &renderPass_);
    }

    void CreatePipeline() {
        if (pipeline_ != VK_NULL_HANDLE) return;

        VkPushConstantRange pushConstant{};
        pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstant.offset = 0;
        pushConstant.size = sizeof(OverlayPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descLayout_;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstant;
        createPipelineLayout_(device_, &layoutInfo, nullptr, &pipelineLayout_);

        VkShaderModule vertShader = VK_NULL_HANDLE;
        VkShaderModule fragShader = VK_NULL_HANDLE;

        VkShaderModuleCreateInfo vertInfo{};
        vertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vertInfo.codeSize = sizeof(overlay_vert_spv);
        vertInfo.pCode = overlay_vert_spv;
        createShaderModule_(device_, &vertInfo, nullptr, &vertShader);

        VkShaderModuleCreateInfo fragInfo{};
        fragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        fragInfo.codeSize = sizeof(overlay_frag_spv);
        fragInfo.pCode = overlay_frag_spv;
        createShaderModule_(device_, &fragInfo, nullptr, &fragShader);

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertShader;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragShader;
        fragStage.pName = "main";

        VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

        VkVertexInputBindingDescription bindingDesc{};
        bindingDesc.binding = 0;
        bindingDesc.stride = sizeof(OverlayVertex);
        bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attribs[3]{};
        attribs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(OverlayVertex, x))};
        attribs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(OverlayVertex, u))};
        attribs[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(offsetof(OverlayVertex, r))};

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &bindingDesc;
        vertexInput.vertexAttributeDescriptionCount = 3;
        vertexInput.pVertexAttributeDescriptions = attribs;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &blendAttachment;

        VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout_;
        pipelineInfo.renderPass = renderPass_;
        pipelineInfo.subpass = 0;

        createGraphicsPipelines_(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_);

        destroyShaderModule_(device_, vertShader, nullptr);
        destroyShaderModule_(device_, fragShader, nullptr);
    }

    void DestroySwapchainLocked(VkSwapchainKHR sc) {
        auto it = swapchains_.find(sc);
        if (it == swapchains_.end()) return;

        if (deviceWaitIdle_) deviceWaitIdle_(device_);

        for (size_t i = 0; i < it->second.views.size(); ++i) {
            if (i < it->second.fences.size() && it->second.fences[i]) {
                destroyFence_(device_, it->second.fences[i], nullptr);
            }
            if (i < it->second.framebuffers.size() && it->second.framebuffers[i]) {
                destroyFramebuffer_(device_, it->second.framebuffers[i], nullptr);
            }
            if (i < it->second.views.size() && it->second.views[i]) {
                destroyImageView_(device_, it->second.views[i], nullptr);
            }
            if (i < it->second.semaphores.size() && it->second.semaphores[i]) {
                destroySemaphore_(device_, it->second.semaphores[i], nullptr);
            }
            if (i < it->second.vertexMapped.size() && it->second.vertexMapped[i]) {
                unmapMemory_(device_, it->second.vertexMemories[i]);
            }
            if (i < it->second.vertexBuffers.size() && it->second.vertexBuffers[i]) {
                destroyBuffer_(device_, it->second.vertexBuffers[i], nullptr);
            }
            if (i < it->second.vertexMemories.size() && it->second.vertexMemories[i]) {
                freeMemory_(device_, it->second.vertexMemories[i], nullptr);
            }
        }
        if (!it->second.cmdBuffers.empty()) {
            freeCommandBuffers_(device_, commandPool_, static_cast<uint32_t>(it->second.cmdBuffers.size()),
                                it->second.cmdBuffers.data());
        }
        swapchains_.erase(it);
    }

    void CleanupLocked() {
        if (!initialized_) return;
        if (device_) {
            if (deviceWaitIdle_) deviceWaitIdle_(device_);

            for (auto& [sc, data] : swapchains_) {
                for (size_t i = 0; i < data.views.size(); ++i) {
                    if (i < data.fences.size() && data.fences[i]) destroyFence_(device_, data.fences[i], nullptr);
                    if (i < data.framebuffers.size() && data.framebuffers[i]) destroyFramebuffer_(device_, data.framebuffers[i], nullptr);
                    if (i < data.views.size() && data.views[i]) destroyImageView_(device_, data.views[i], nullptr);
                    if (i < data.semaphores.size() && data.semaphores[i]) destroySemaphore_(device_, data.semaphores[i], nullptr);
                    if (i < data.vertexMapped.size() && data.vertexMapped[i]) unmapMemory_(device_, data.vertexMemories[i]);
                    if (i < data.vertexBuffers.size() && data.vertexBuffers[i]) destroyBuffer_(device_, data.vertexBuffers[i], nullptr);
                    if (i < data.vertexMemories.size() && data.vertexMemories[i]) freeMemory_(device_, data.vertexMemories[i], nullptr);
                }
                if (!data.cmdBuffers.empty()) {
                    freeCommandBuffers_(device_, commandPool_, static_cast<uint32_t>(data.cmdBuffers.size()), data.cmdBuffers.data());
                }
            }
            swapchains_.clear();

            if (pipeline_ && destroyPipeline_) destroyPipeline_(device_, pipeline_, nullptr);
            if (pipelineLayout_ && destroyPipelineLayout_) destroyPipelineLayout_(device_, pipelineLayout_, nullptr);
            if (renderPass_ && destroyRenderPass_) destroyRenderPass_(device_, renderPass_, nullptr);

            if (descPool_ && destroyDescriptorPool_) destroyDescriptorPool_(device_, descPool_, nullptr);
            if (descLayout_ && destroyDescriptorSetLayout_) destroyDescriptorSetLayout_(device_, descLayout_, nullptr);
            if (fontSampler_ && destroySampler_) destroySampler_(device_, fontSampler_, nullptr);
            if (fontView_ && destroyImageView_) destroyImageView_(device_, fontView_, nullptr);
            if (fontImage_ && destroyImage_) destroyImage_(device_, fontImage_, nullptr);
            if (fontMemory_ && freeMemory_) freeMemory_(device_, fontMemory_, nullptr);

            if (commandPool_ && destroyCommandPool_) destroyCommandPool_(device_, commandPool_, nullptr);
        }

        pipeline_ = VK_NULL_HANDLE;
        pipelineLayout_ = VK_NULL_HANDLE;
        renderPass_ = VK_NULL_HANDLE;
        descPool_ = VK_NULL_HANDLE;
        descLayout_ = VK_NULL_HANDLE;
        fontSampler_ = VK_NULL_HANDLE;
        fontView_ = VK_NULL_HANDLE;
        fontImage_ = VK_NULL_HANDLE;
        fontMemory_ = VK_NULL_HANDLE;
        commandPool_ = VK_NULL_HANDLE;
        frametimes_.clear();
        initialized_ = false;
    }

    mutable std::mutex mutex_;
    bool initialized_ = false;
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physDevice_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    PFN_vkGetDeviceProcAddr gdpa_ = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties getMemProps_ = nullptr;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkImage fontImage_ = VK_NULL_HANDLE;
    VkDeviceMemory fontMemory_ = VK_NULL_HANDLE;
    VkImageView fontView_ = VK_NULL_HANDLE;
    VkSampler fontSampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descSet_ = VK_NULL_HANDLE;

    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    std::unordered_map<VkSwapchainKHR, SwapchainData> swapchains_;

    // Function pointers
    PFN_vkCreateCommandPool createCommandPool_ = nullptr;
    PFN_vkDestroyCommandPool destroyCommandPool_ = nullptr;
    PFN_vkCreateBuffer createBuffer_ = nullptr;
    PFN_vkDestroyBuffer destroyBuffer_ = nullptr;
    PFN_vkGetBufferMemoryRequirements getBufferMemoryRequirements_ = nullptr;
    PFN_vkAllocateMemory allocateMemory_ = nullptr;
    PFN_vkFreeMemory freeMemory_ = nullptr;
    PFN_vkBindBufferMemory bindBufferMemory_ = nullptr;
    PFN_vkMapMemory mapMemory_ = nullptr;
    PFN_vkUnmapMemory unmapMemory_ = nullptr;
    PFN_vkCreateImage createImage_ = nullptr;
    PFN_vkDestroyImage destroyImage_ = nullptr;
    PFN_vkGetImageMemoryRequirements getImageMemoryRequirements_ = nullptr;
    PFN_vkBindImageMemory bindImageMemory_ = nullptr;
    PFN_vkCreateImageView createImageView_ = nullptr;
    PFN_vkDestroyImageView destroyImageView_ = nullptr;
    PFN_vkCreateSampler createSampler_ = nullptr;
    PFN_vkDestroySampler destroySampler_ = nullptr;
    PFN_vkCreateDescriptorSetLayout createDescriptorSetLayout_ = nullptr;
    PFN_vkDestroyDescriptorSetLayout destroyDescriptorSetLayout_ = nullptr;
    PFN_vkCreateDescriptorPool createDescriptorPool_ = nullptr;
    PFN_vkDestroyDescriptorPool destroyDescriptorPool_ = nullptr;
    PFN_vkAllocateDescriptorSets allocateDescriptorSets_ = nullptr;
    PFN_vkUpdateDescriptorSets updateDescriptorSets_ = nullptr;
    PFN_vkCreateRenderPass createRenderPass_ = nullptr;
    PFN_vkDestroyRenderPass destroyRenderPass_ = nullptr;
    PFN_vkCreateShaderModule createShaderModule_ = nullptr;
    PFN_vkDestroyShaderModule destroyShaderModule_ = nullptr;
    PFN_vkCreatePipelineLayout createPipelineLayout_ = nullptr;
    PFN_vkDestroyPipelineLayout destroyPipelineLayout_ = nullptr;
    PFN_vkCreateGraphicsPipelines createGraphicsPipelines_ = nullptr;
    PFN_vkDestroyPipeline destroyPipeline_ = nullptr;
    PFN_vkCreateFramebuffer createFramebuffer_ = nullptr;
    PFN_vkDestroyFramebuffer destroyFramebuffer_ = nullptr;
    PFN_vkCreateFence createFence_ = nullptr;
    PFN_vkDestroyFence destroyFence_ = nullptr;
    PFN_vkWaitForFences waitForFences_ = nullptr;
    PFN_vkResetFences resetFences_ = nullptr;
    PFN_vkAllocateCommandBuffers allocateCommandBuffers_ = nullptr;
    PFN_vkFreeCommandBuffers freeCommandBuffers_ = nullptr;
    PFN_vkResetCommandBuffer resetCommandBuffer_ = nullptr;
    PFN_vkBeginCommandBuffer beginCommandBuffer_ = nullptr;
    PFN_vkEndCommandBuffer endCommandBuffer_ = nullptr;
    PFN_vkCmdPipelineBarrier cmdPipelineBarrier_ = nullptr;
    PFN_vkCmdCopyBufferToImage cmdCopyBufferToImage_ = nullptr;
    PFN_vkCmdBeginRenderPass cmdBeginRenderPass_ = nullptr;
    PFN_vkCmdEndRenderPass cmdEndRenderPass_ = nullptr;
    PFN_vkCmdBindPipeline cmdBindPipeline_ = nullptr;
    PFN_vkCmdSetViewport cmdSetViewport_ = nullptr;
    PFN_vkCmdSetScissor cmdSetScissor_ = nullptr;
    PFN_vkCmdPushConstants cmdPushConstants_ = nullptr;
    PFN_vkCmdBindDescriptorSets cmdBindDescriptorSets_ = nullptr;
    PFN_vkCmdBindVertexBuffers cmdBindVertexBuffers_ = nullptr;
    PFN_vkCmdDraw cmdDraw_ = nullptr;
    PFN_vkCreateSemaphore createSemaphore_ = nullptr;
    PFN_vkDestroySemaphore destroySemaphore_ = nullptr;
    PFN_vkQueueSubmit queueSubmit_ = nullptr;
    PFN_vkQueueWaitIdle queueWaitIdle_ = nullptr;
    PFN_vkDeviceWaitIdle deviceWaitIdle_ = nullptr;
    PFN_vkFlushMappedMemoryRanges flushMappedMemoryRanges_ = nullptr;
};

} // namespace gnumon::layer
