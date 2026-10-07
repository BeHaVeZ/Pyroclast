#include "vulkan_renderer.hpp"
#include "vulkan_resources.hpp"
#include "../draw_validation.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
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

VulkanUpscale::~VulkanUpscale()
{
    if (!device) return;
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, layout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptors, nullptr);
    vkDestroySampler(device, sampler, nullptr);
}

VulkanTiming::~VulkanTiming()
{
    if (device) vkDestroyQueryPool(device, pool, nullptr);
}

VulkanRenderer::~VulkanRenderer()
{
    wait_idle();
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

void VulkanRenderer::draw_range(VkCommandBuffer command, const DrawRange& range, const Buffer& buffer, float aspect)
{
    const auto& vertices = static_cast<const VulkanBuffer&>(buffer);
    const auto& pipeline = static_cast<const VulkanGraphicsPipeline&>(range.pipeline);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
    const VkDeviceSize offset = static_cast<VkDeviceSize>(range.first_record) * particle_layout::particle_stride;
    vkCmdBindVertexBuffers(command, 0, 1, &vertices.buffer, &offset);
    vkCmdPushConstants(command, pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float), &aspect);
    vkCmdPushConstants(command, pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(float), sizeof(std::uint32_t), &range.layer);
    vkCmdDraw(command, 6, range.record_count, 0, 0);
}

void transition_image(VkCommandBuffer command, VkImage image, VkImageLayout from, VkImageLayout to,
    VkPipelineStageFlags2 source_stage, VkAccessFlags2 source_access,
    VkPipelineStageFlags2 destination_stage, VkAccessFlags2 destination_access)
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
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
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

namespace
{
void execution_barrier(VkCommandBuffer command)
{
    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(command, &dependency);
}
}

RenderResult VulkanRenderer::render(const Frame& frame)
{
    const auto* vertices = dynamic_cast<const VulkanBuffer*>(&frame.vertices);
    const auto* compute = frame.compute ? dynamic_cast<const VulkanComputePipeline*>(&frame.compute->pipeline) : nullptr;
    bool owned = vertices && vertices->owner == this && (!frame.compute || (compute && compute->owner == this));
    const auto owns_target = [this](const RenderTarget* target) {
        const auto* native = dynamic_cast<const VulkanRenderTarget*>(target);
        return target == nullptr || (native && native->owner == this);
    };
    for (const auto& pass : frame.passes)
    {
        owned = owned && owns_target(pass.target) && owns_target(pass.background);
        for (const auto& range : pass.draws)
        {
            const auto* pipeline = dynamic_cast<const VulkanGraphicsPipeline*>(&range.pipeline);
            owned = owned && pipeline && pipeline->owner == this;
        }
    }
    const char* error = !owned ? "Frame uses resources from another graphics device." :
        frame.capture && !capture_enabled_ ? "Frame capture requires a device created with capture enabled." :
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
        if (pass.target && !size_target(static_cast<VulkanRenderTarget&>(*pass.target)))
        {
            return RenderResult::failed;
        }
    }
    auto& slot = frames_[frame_index_];
    if (!check(vkWaitForFences(vk_.device, 1, &slot.submitted, VK_TRUE,
        std::numeric_limits<std::uint64_t>::max()), "Wait for frame command buffer") || !read_timestamps(frame_index_))
    {
        return RenderResult::failed;
    }
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
    if (!check(vkResetCommandPool(vk_.device, slot.pool, 0), "Reset frame command pool"))
    {
        return RenderResult::failed;
    }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!check(vkBeginCommandBuffer(slot.command, &begin), "Begin Vulkan command buffer"))
    {
        return RenderResult::failed;
    }
    const auto command = slot.command;
    const auto query = static_cast<std::uint32_t>(frame_index_) * 4;
    if (timing_.pool)
    {
        vkCmdResetQueryPool(command, timing_.pool, query, 4);
        vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timing_.pool, query);
        execution_barrier(command);
    }
    if (compute)
    {
        transition_buffer(command, vertices->buffer,
            VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, compute->pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, compute->layout,
            0, 1, &compute->descriptor, 0, nullptr);
        vkCmdPushConstants(command, compute->layout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, 4 * sizeof(std::uint32_t), frame.compute->constants.data());
        vkCmdDispatch(command, frame.compute->group_count, 1, 1);
        transition_buffer(command, vertices->buffer,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
    }
    if (timing_.pool)
    {
        vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timing_.pool, query + 1);
        vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timing_.pool, query + 2);
        execution_barrier(command);
    }
    for (const auto& pass : frame.passes)
    {
        const auto* target = static_cast<const VulkanRenderTarget*>(pass.target);
        if (target)
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
        const auto extent = target ? target->extent : swapchain_.extent;
        const auto& clear_color = target ? target->description().clear_color : frame.clear_color;
        VkRenderingAttachmentInfo color{};
        color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color.imageView = target ? target->view : image.view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        std::copy(clear_color.begin(), clear_color.end(), color.clearValue.color.float32);
        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.extent = extent;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        vkCmdBeginRendering(command, &rendering);
        set_viewport(command, extent);
        if (pass.background) upscale(command, static_cast<const VulkanRenderTarget&>(*pass.background));
        for (const auto& range : pass.draws) draw_range(command, range, *vertices, frame.aspect_ratio);
        vkCmdEndRendering(command);
        if (target)
        {
            transition_image(command, target->image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }
    }
    if (!frame.capture)
    {
        transition_image(command, image.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
    }
    if (timing_.pool) vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timing_.pool, query + 3);
    if (frame.capture && !record_capture(command, image.image))
    {
        return RenderResult::failed;
    }
    if (!check(vkEndCommandBuffer(command), "End Vulkan command buffer") ||
        !check(vkResetFences(vk_.device, 1, &slot.submitted), "Reset frame fence") ||
        !check(vkResetFences(vk_.device, 1, &image.presented), "Reset presentation fence"))
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
    VkSwapchainPresentFenceInfoEXT fence{};
    fence.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT;
    fence.swapchainCount = 1;
    fence.pFences = &image.presented;
    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.pNext = &fence;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &image.rendered;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_.handle;
    present.pImageIndices = &image_index;
    const VkResult result = vkQueuePresentKHR(vk_.queue, &present);
    image.present_pending = result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR ||
        result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR;
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
}
