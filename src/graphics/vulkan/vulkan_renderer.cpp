#include "vulkan_renderer.hpp"
#include "vulkan_resources.hpp"
#include "../draw_validation.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <limits>

namespace pyroclast
{
VulkanRenderer::~VulkanRenderer()
{
    if (device_)
    {
        wait_idle();
        destroy_swapchain();
        vkDestroyPipeline(device_, upscale_pipeline_, nullptr);
        vkDestroyPipelineLayout(device_, upscale_layout_, nullptr);
        vkDestroyDescriptorSetLayout(device_, upscale_descriptors_, nullptr);
        vkDestroySampler(device_, upscale_sampler_, nullptr);
        vkDestroyRenderPass(device_, offscreen_pass_, nullptr);
        vkDestroyRenderPass(device_, render_pass_, nullptr);
        for (const auto& slot : frames_)
        {
            vkDestroyCommandPool(device_, slot.pool, nullptr);
            vkDestroySemaphore(device_, slot.acquired, nullptr);
            vkDestroyFence(device_, slot.submitted, nullptr);
        }
        vkDestroyQueryPool(device_, timestamps_, nullptr);
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_)
    {
        SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
    }
    if (messenger_)
    {
        const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy)
        {
            destroy(instance_, messenger_, nullptr);
        }
    }
    if (instance_)
    {
        vkDestroyInstance(instance_, nullptr);
    }
#ifndef NDEBUG
    SDL_Log("Vulkan validation summary: %llu warnings, %llu errors",
        static_cast<unsigned long long>(validation_warnings_.load()),
        static_cast<unsigned long long>(validation_errors_.load()));
#endif
}

bool VulkanRenderer::wait_idle()
{
    if (!device_)
    {
        return true;
    }
    if (!check(vkDeviceWaitIdle(device_), "Wait for Vulkan rendering"))
    {
        return false;
    }
    for (auto& image : images_)
    {
        if (image.present_pending)
        {
            if (!check(vkWaitForFences(device_, 1, &image.presented, VK_TRUE,
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

void VulkanRenderer::draw_range(VkCommandBuffer command, const DrawRange& range, const VertexBuffer& buffer, float aspect)
{
    const auto& vertices = static_cast<const VulkanVertexBuffer&>(buffer);
    const auto& pipeline = static_cast<const VulkanGraphicsPipeline&>(range.pipeline);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
    const VkDeviceSize offset = static_cast<VkDeviceSize>(range.first_record) * vertices.stride();
    vkCmdBindVertexBuffers(command, 0, 1, &vertices.buffer, &offset);
    vkCmdPushConstants(command, pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float), &aspect);
    vkCmdPushConstants(command, pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(float), sizeof(std::uint32_t), &range.layer);
    vkCmdDraw(command, 6, range.record_count, 0, 0);
}

RenderResult VulkanRenderer::render(const Frame& frame)
{
    const auto* vertices = dynamic_cast<const VulkanVertexBuffer*>(&frame.vertices);
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
    const char* error = owned ? validate_frame(frame) : "Frame uses resources from another graphics device.";
    if (error)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error);
        return RenderResult::failed;
    }
    if (!validation_ok())
    {
        return RenderResult::failed;
    }
    if (!swapchain_ || extent_.width == 0 || extent_.height == 0)
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
    if (!check(vkWaitForFences(device_, 1, &slot.submitted, VK_TRUE,
        std::numeric_limits<std::uint64_t>::max()), "Wait for frame command buffer") || !read_timestamps(frame_index_))
    {
        return RenderResult::failed;
    }
    std::uint32_t image_index = 0;
    const VkResult acquired = vkAcquireNextImageKHR(device_, swapchain_,
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
    auto& image = images_[image_index];
    if (image.present_pending && !check(vkWaitForFences(device_, 1, &image.presented, VK_TRUE,
        std::numeric_limits<std::uint64_t>::max()), "Wait for image presentation fence"))
    {
        return RenderResult::failed;
    }
    image.present_pending = false;
    if (!check(vkResetCommandPool(device_, slot.pool, 0), "Reset frame command pool"))
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
    const auto query = static_cast<std::uint32_t>(frame_index_) * 4;
    if (timestamps_)
    {
        vkCmdResetQueryPool(slot.command, timestamps_, query, 4);
        vkCmdWriteTimestamp(slot.command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamps_, query);
        vkCmdPipelineBarrier(slot.command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, nullptr, 0, nullptr, 0, nullptr);
    }
    if (compute)
    {
        VkBufferMemoryBarrier storage{};
        storage.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        storage.srcAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        storage.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        storage.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        storage.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        storage.buffer = vertices->buffer;
        storage.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(slot.command, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &storage, 0, nullptr);
        vkCmdBindPipeline(slot.command, VK_PIPELINE_BIND_POINT_COMPUTE, compute->pipeline);
        vkCmdBindDescriptorSets(slot.command, VK_PIPELINE_BIND_POINT_COMPUTE, compute->layout,
            0, 1, &compute->descriptor, 0, nullptr);
        vkCmdPushConstants(slot.command, compute->layout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, 4 * sizeof(std::uint32_t), frame.compute->constants.data());
        vkCmdDispatch(slot.command, frame.compute->group_count, 1, 1);
        storage.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        storage.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        vkCmdPipelineBarrier(slot.command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr, 1, &storage, 0, nullptr);
    }
    if (timestamps_)
    {
        vkCmdWriteTimestamp(slot.command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamps_, query + 1);
        vkCmdWriteTimestamp(slot.command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamps_, query + 2);
        vkCmdPipelineBarrier(slot.command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, nullptr, 0, nullptr, 0, nullptr);
    }
    for (const auto& pass : frame.passes)
    {
        const auto* target = static_cast<const VulkanRenderTarget*>(pass.target);
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (target)
        {
            barrier.image = target->image;
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            vkCmdPipelineBarrier(slot.command, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
        const auto extent = target ? target->extent : extent_;
        VkClearValue clear{};
        const auto& clear_color = target ? target->description().clear_color : frame.clear_color;
        std::copy(clear_color.begin(), clear_color.end(), clear.color.float32);
        VkRenderPassBeginInfo begin_pass{};
        begin_pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin_pass.renderPass = target ? offscreen_pass_ : render_pass_;
        begin_pass.framebuffer = target ? target->framebuffer : image.framebuffer;
        begin_pass.renderArea.extent = extent;
        begin_pass.clearValueCount = 1;
        begin_pass.pClearValues = &clear;
        vkCmdBeginRenderPass(slot.command, &begin_pass, VK_SUBPASS_CONTENTS_INLINE);
        set_viewport(slot.command, extent);
        if (pass.background) upscale(slot.command, static_cast<const VulkanRenderTarget&>(*pass.background));
        for (const auto& range : pass.draws) draw_range(slot.command, range, *vertices, frame.aspect_ratio);
        vkCmdEndRenderPass(slot.command);
        if (target)
        {
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vkCmdPipelineBarrier(slot.command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
    }
    if (timestamps_) vkCmdWriteTimestamp(slot.command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamps_, query + 3);
    if (!check(vkEndCommandBuffer(slot.command), "End Vulkan command buffer") ||
        !check(vkResetFences(device_, 1, &slot.submitted), "Reset frame fence") ||
        !check(vkResetFences(device_, 1, &image.presented), "Reset presentation fence"))
    {
        return RenderResult::failed;
    }
    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &slot.acquired;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &slot.command;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &image.rendered;
    if (!check(vkQueueSubmit(queue_, 1, &submit, slot.submitted), "Submit Vulkan frame"))
    {
        return RenderResult::failed;
    }
    slot.timestamps_pending = timestamps_ != VK_NULL_HANDLE;
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
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &image_index;
    const VkResult result = vkQueuePresentKHR(queue_, &present);
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
