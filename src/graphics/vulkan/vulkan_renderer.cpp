#include "vulkan_renderer.hpp"
#include "vulkan_resources.hpp"
#include "../draw_validation.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace pyroclast
{
VulkanContext::~VulkanContext()
{
    if (device)
    {
        vkDestroyDevice(device, nullptr);
    }
    if (surface)
    {
        SDL_Vulkan_DestroySurface(instance, surface, nullptr);
    }
    if (messenger)
    {
        const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy)
        {
            destroy(instance, messenger, nullptr);
        }
    }
    if (instance)
    {
        vkDestroyInstance(instance, nullptr);
    }
#ifndef NDEBUG
    SDL_Log("Vulkan validation summary: %llu warnings, %llu errors",
        static_cast<unsigned long long>(validation_warnings.load()),
        static_cast<unsigned long long>(validation_errors.load()));
#endif
}

VulkanFrameSlot::~VulkanFrameSlot()
{
    if (!device) return;
    vkDestroyCommandPool(device, pool, nullptr);
    vkDestroySemaphore(device, acquired, nullptr);
    vkDestroyFence(device, submitted, nullptr);
    vkDestroyBuffer(device, constants, nullptr);
    vkFreeMemory(device, constants_memory, nullptr);
    vkDestroyBuffer(device, readback, nullptr);
    vkFreeMemory(device, readback_memory, nullptr);
}

void VulkanSwapchain::reset()
{
    if (!device) return;
    for (auto& image : images)
    {
        vkDestroyImageView(device, image.view, nullptr);
        vkDestroySemaphore(device, image.rendered, nullptr);
        vkDestroyFence(device, image.presented, nullptr);
    }
    images.clear();
    vkDestroySwapchainKHR(device, handle, nullptr);
    handle = VK_NULL_HANDLE;
    extent = {};
}

VulkanTiming::~VulkanTiming()
{
    if (device) vkDestroyQueryPool(device, pool, nullptr);
}

VulkanRenderer::~VulkanRenderer()
{
    wait_idle();
    destroy_overlay();
}

bool VulkanRenderer::wait_idle()
{
    if (!vk_.device)
    {
        return true;
    }
    if (!check(vkDeviceWaitIdle(vk_.device), "Wait for Vulkan rendering"))
    {
        return false;
    }
    for (auto& image : swapchain_.images)
    {
        if (image.present_pending)
        {
            if (!check(vkWaitForFences(vk_.device, 1, &image.presented, VK_TRUE,
                std::numeric_limits<std::uint64_t>::max()), "Wait for Vulkan presentation"))
            {
                return false;
            }
            image.present_pending = false;
        }
    }
    return validation_ok();
}

void VulkanRenderer::set_viewport(VkCommandBuffer command, VkExtent2D extent)
{
    const VkViewport viewport{0, static_cast<float>(extent.height), static_cast<float>(extent.width),
        -static_cast<float>(extent.height), 0, 1};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
}

void VulkanRenderer::begin_label(VkCommandBuffer command, const char* label) const
{
    if (!label || !vk_.begin_label) return;
    VkDebugUtilsLabelEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    info.pLabelName = label;
    vk_.begin_label(command, &info);
}

void VulkanRenderer::end_label(VkCommandBuffer command, const char* label) const
{
    if (label && vk_.end_label) vk_.end_label(command);
}

void transition_image(VkCommandBuffer command, VkImage image, VkImageLayout from, VkImageLayout to,
    VkPipelineStageFlags2 source_stage, VkAccessFlags2 source_access,
    VkPipelineStageFlags2 destination_stage, VkAccessFlags2 destination_access, VkImageAspectFlags aspect)
{
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = source_stage;
    barrier.srcAccessMask = source_access;
    barrier.dstStageMask = destination_stage;
    barrier.dstAccessMask = destination_access;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, 1, 0, 1};
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(command, &dependency);
}

void transition_buffer(VkCommandBuffer command, VkBuffer buffer,
    VkPipelineStageFlags2 source_stage, VkAccessFlags2 source_access,
    VkPipelineStageFlags2 destination_stage, VkAccessFlags2 destination_access)
{
    VkBufferMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = source_stage;
    barrier.srcAccessMask = source_access;
    barrier.dstStageMask = destination_stage;
    barrier.dstAccessMask = destination_access;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(command, &dependency);
}

void memory_barrier(VkCommandBuffer command,
    VkPipelineStageFlags2 source_stage, VkAccessFlags2 source_access,
    VkPipelineStageFlags2 destination_stage, VkAccessFlags2 destination_access)
{
    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = source_stage;
    barrier.srcAccessMask = source_access;
    barrier.dstStageMask = destination_stage;
    barrier.dstAccessMask = destination_access;
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(command, &dependency);
}

bool VulkanRenderer::owns(const Frame& frame) const
{
    const auto buffer_owned = [this](const Buffer* buffer) {
        const auto* resource = dynamic_cast<const VulkanBuffer*>(buffer);
        return buffer == nullptr || (resource && resource->owner == this);
    };
    const auto buffers_owned = [&buffer_owned](const Bindings& bindings) {
        return std::all_of(bindings.read.begin(), bindings.read.end(), buffer_owned) &&
            std::all_of(bindings.write.begin(), bindings.write.end(), buffer_owned);
    };
    const auto target_owned = [this](const RenderTarget* target) {
        const auto* native_target = dynamic_cast<const VulkanRenderTarget*>(target);
        return target == nullptr || (native_target && native_target->owner == this);
    };
    if (!buffer_owned(frame.readback.source)) return false;
    for (const auto& dispatch : frame.dispatches)
    {
        const auto* pipeline = dynamic_cast<const VulkanComputePipeline*>(&dispatch.pipeline);
        if (!pipeline || pipeline->owner != this || !buffers_owned(dispatch.bindings)) return false;
    }
    for (const auto& pass : frame.passes)
    {
        if (!target_owned(pass.target) || !target_owned(pass.depth) ||
            !std::all_of(pass.inputs.begin(), pass.inputs.end(), target_owned)) return false;
        for (const auto& draw : pass.draws)
        {
            const auto* pipeline = dynamic_cast<const VulkanGraphicsPipeline*>(&draw.pipeline);
            if (!pipeline || pipeline->owner != this || !buffers_owned(draw.bindings) || !buffer_owned(draw.indirect)) return false;
        }
    }
    return true;
}

RenderResult VulkanRenderer::render(const Frame& frame)
{
    const char* error = !owns(frame) ? "Frame uses resources from another graphics device." :
        frame.capture && !capture_enabled_ ? "Frame capture requires a device created with capture enabled." :
        frame.overlay && !overlay_ ? "Frame overlay requires a device created with the debug overlay." :
        validate_frame(frame);
    if (error)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error);
        return RenderResult::failed;
    }
    if (!validation_ok())
    {
        return RenderResult::failed;
    }
    if (!swapchain_.handle || swapchain_.extent.width == 0 || swapchain_.extent.height == 0)
    {
        SDL_Delay(20);
        return RenderResult::skipped;
    }
    for (const auto& pass : frame.passes)
    {
        if ((pass.target && !size_target(static_cast<VulkanRenderTarget&>(*pass.target))) ||
            (pass.depth && !size_target(static_cast<VulkanRenderTarget&>(*pass.depth))))
        {
            return RenderResult::failed;
        }
    }
    auto& slot = frames_[frame_index_];
    const auto fence_begin = SDL_GetPerformanceCounter();
    if (!check(vkWaitForFences(vk_.device, 1, &slot.submitted, VK_TRUE,
        std::numeric_limits<std::uint64_t>::max()), "Wait for frame command buffer"))
    {
        return RenderResult::failed;
    }
    const auto fence_end = SDL_GetPerformanceCounter();
    if (!read_timestamps(frame_index_))
    {
        return RenderResult::failed;
    }
    read_readback(slot);
    const auto acquire_begin = SDL_GetPerformanceCounter();
    std::uint32_t image_index = 0;
    const VkResult acquired = vkAcquireNextImageKHR(vk_.device, swapchain_.handle,
        std::numeric_limits<std::uint64_t>::max(), slot.acquired, VK_NULL_HANDLE, &image_index);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR)
    {
        rebuild_ = true;
        return RenderResult::skipped;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR)
    {
        check(acquired, "Acquire Vulkan image");
        return RenderResult::failed;
    }
    rebuild_ = acquired == VK_SUBOPTIMAL_KHR;
    auto& image = swapchain_.images[image_index];
    if (image.present_pending && !check(vkWaitForFences(vk_.device, 1, &image.presented, VK_TRUE,
        std::numeric_limits<std::uint64_t>::max()), "Wait for image presentation fence"))
    {
        return RenderResult::failed;
    }
    image.present_pending = false;
    const auto wait_end = SDL_GetPerformanceCounter();
    if (!check(vkResetCommandPool(vk_.device, slot.pool, 0), "Reset frame command pool"))
    {
        return RenderResult::failed;
    }
    std::memcpy(slot.mapped_constants, frame.constants.data(), frame.constants.size());
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!check(vkBeginCommandBuffer(slot.command, &begin), "Begin Vulkan command buffer"))
    {
        return RenderResult::failed;
    }
    const auto command = slot.command;
    constexpr VkShaderStageFlags constant_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    constexpr std::uint32_t constant_bytes = binding_layout::draw_constant_words * sizeof(std::uint32_t);
    slot.region_count = 0;
    const auto query = static_cast<std::uint32_t>(frame_index_) * queries_per_slot;
    if (timing_.pool)
    {
        vkCmdResetQueryPool(command, timing_.pool, query, queries_per_slot);
        vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timing_.pool, query);
        memory_barrier(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);
    }
    constexpr VkPipelineStageFlags2 shader_stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    for (std::size_t index = 0; index < frame.dispatches.size(); ++index)
    {
        const auto& dispatch = frame.dispatches[index];
        const auto region = begin_region(command, dispatch.label, "Dispatch");
        if (index == 0)
        {
            memory_barrier(command, shader_stages | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        }
        else
        {
            memory_barrier(command, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        }
        begin_label(command, dispatch.label);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, static_cast<const VulkanComputePipeline&>(dispatch.pipeline).pipeline);
        vkCmdPushConstants(command, bindings_.layout, constant_stages, 0, constant_bytes, dispatch.constants.data());
        push_bindings(command, VK_PIPELINE_BIND_POINT_COMPUTE, dispatch.bindings, slot, {});
        vkCmdDispatch(command, dispatch.groups[0], dispatch.groups[1], dispatch.groups[2]);
        end_label(command, dispatch.label);
        end_region(command, region);
    }
    if (!frame.dispatches.empty())
    {
        memory_barrier(command, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT);
    }
    if (timing_.pool) vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timing_.pool, query + 1);
    if (frame.readback.source)
    {
        const VkBufferCopy region{frame.readback.offset, 0, frame.readback.size};
        vkCmdCopyBuffer(command, static_cast<const VulkanBuffer*>(frame.readback.source)->buffer, slot.readback, 1, &region);
        transition_buffer(command, slot.readback, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        slot.readback_size = frame.readback.size;
    }
    if (timing_.pool)
    {
        vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timing_.pool, query + 2);
        memory_barrier(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);
    }
    constexpr VkPipelineStageFlags2 depth_stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    for (const auto& pass : frame.passes)
    {
        const auto* target = static_cast<const VulkanRenderTarget*>(pass.target);
        const auto* depth = static_cast<const VulkanRenderTarget*>(pass.depth);
        const auto region = begin_region(command, pass.label, "Pass");
        begin_label(command, pass.label);
        if (target && pass.load)
        {
            transition_image(command, target->image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        }
        else if (target)
        {
            transition_image(command, target->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        }
        else
        {
            transition_image(command, image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        }
        if (depth && pass.depth_write)
        {
            transition_image(command, depth->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                depth_stages | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE,
                depth_stages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT);
        }
        const auto extent = target ? target->extent : swapchain_.extent;
        const auto& clear_color = target ? target->description().clear_color : frame.clear_color;
        VkRenderingAttachmentInfo color{};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = target ? target->view : image.view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = pass.load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        std::copy(clear_color.begin(), clear_color.end(), color.clearValue.color.float32);
        VkRenderingAttachmentInfo depth_attachment{};
        depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        if (depth)
        {
            depth_attachment.imageView = depth->view;
            depth_attachment.imageLayout = pass.depth_write ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL :
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            depth_attachment.loadOp = pass.depth_write ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            depth_attachment.storeOp = pass.depth_write ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_NONE;
            depth_attachment.clearValue.depthStencil = {depth->description().clear_depth, 0};
        }
        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.extent = extent;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        rendering.pDepthAttachment = depth ? &depth_attachment : nullptr;
        vkCmdBeginRendering(command, &rendering);
        set_viewport(command, extent);
        for (const auto& draw : pass.draws)
        {
            begin_label(command, draw.label);
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, static_cast<const VulkanGraphicsPipeline&>(draw.pipeline).pipeline);
            vkCmdPushConstants(command, bindings_.layout, constant_stages, 0, constant_bytes, draw.constants.data());
            push_bindings(command, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.bindings, slot, pass.inputs);
            if (draw.indirect)
            {
                vkCmdDrawIndirect(command, static_cast<const VulkanBuffer*>(draw.indirect)->buffer, draw.indirect_offset, 1, 0);
            }
            else
            {
                vkCmdDraw(command, draw.vertex_count, draw.instance_count, 0, 0);
            }
            end_label(command, draw.label);
        }
        vkCmdEndRendering(command);
        end_region(command, region);
        if (target)
        {
            transition_image(command, target->image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }
        if (depth && pass.depth_write)
        {
            transition_image(command, depth->image, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, depth_stages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                depth_stages | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        }
        end_label(command, pass.label);
    }
    if (frame.overlay)
    {
        if (timing_.pool) vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timing_.pool, query + 3);
        const auto region = begin_region(command, "Overlay", "Overlay");
        begin_label(command, "Overlay");
        record_overlay(command, frame.overlay, image.view, swapchain_.extent);
        end_label(command, "Overlay");
        end_region(command, region);
    }
    if (!frame.capture)
    {
        transition_image(command, image.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
    }
    if (timing_.pool && !frame.overlay) vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timing_.pool, query + 3);
    if (frame.capture && !record_capture(command, image.image))
    {
        return RenderResult::failed;
    }
    if (!check(vkEndCommandBuffer(command), "End Vulkan command buffer") ||
        !check(vkResetFences(vk_.device, 1, &slot.submitted), "Reset frame fence") ||
        (vk_.present_fences && !check(vkResetFences(vk_.device, 1, &image.presented), "Reset presentation fence")))
    {
        return RenderResult::failed;
    }
    VkSemaphoreSubmitInfo wait{};
    wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait.semaphore = slot.acquired;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signal{};
    signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal.semaphore = image.rendered;
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo buffer{};
    buffer.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    buffer.commandBuffer = command;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &buffer;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    if (!check(vkQueueSubmit2(vk_.queue, 1, &submit, slot.submitted), "Submit Vulkan frame"))
    {
        return RenderResult::failed;
    }
    slot.timestamps_pending = timing_.pool != VK_NULL_HANDLE;
    slot.readback_pending = frame.readback.source != nullptr;
    VkSwapchainPresentFenceInfoEXT fence{};
    fence.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT;
    fence.swapchainCount = 1;
    fence.pFences = &image.presented;
    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.pNext = vk_.present_fences ? &fence : nullptr;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &image.rendered;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_.handle;
    present.pImageIndices = &image_index;
    const auto present_begin = SDL_GetPerformanceCounter();
    const VkResult result = vkQueuePresentKHR(vk_.queue, &present);
    const auto present_end = SDL_GetPerformanceCounter();
    const double milliseconds = 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
    submit_timings_ = {static_cast<double>((fence_end - fence_begin) + (wait_end - acquire_begin)) * milliseconds,
        static_cast<double>(present_end - present_begin) * milliseconds};
    image.present_pending = vk_.present_fences && (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR ||
        result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR);
    frame_index_ = (frame_index_ + 1) % frames_.size();
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        rebuild_ = true;
    }
    else if (!check(result, "Present Vulkan frame"))
    {
        return RenderResult::failed;
    }
    return validation_ok() ? RenderResult::submitted : RenderResult::failed;
}

std::optional<GpuMemory> VulkanRenderer::memory_usage() const
{
    if (!vk_.memory_budget) return std::nullopt;
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{};
    budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
    VkPhysicalDeviceMemoryProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    properties.pNext = &budget;
    vkGetPhysicalDeviceMemoryProperties2(vk_.physical, &properties);
    GpuMemory memory;
    for (std::uint32_t heap = 0; heap < properties.memoryProperties.memoryHeapCount; ++heap)
    {
        const bool local = (properties.memoryProperties.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
        (local ? memory.local_usage : memory.non_local_usage) += budget.heapUsage[heap];
        (local ? memory.local_budget : memory.non_local_budget) += budget.heapBudget[heap];
    }
    return memory;
}
}
