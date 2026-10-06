#include "vulkan_renderer.hpp"
#include "vulkan_resources.hpp"

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
        vkDestroyRenderPass(device_, render_pass_, nullptr);
        for (const auto& frame : frames_)
        {
            vkDestroyCommandPool(device_, frame.pool, nullptr);
            vkDestroySemaphore(device_, frame.acquired, nullptr);
            vkDestroyFence(device_, frame.submitted, nullptr);
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

RenderResult VulkanRenderer::render(const DrawCommand& draw)
{
    const auto* vertices = dynamic_cast<const VulkanVertexBuffer*>(&draw.vertices);
    if (!vertices || vertices->owner != this || draw.ranges.empty())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Draw requires an owned vertex buffer and at least one range.");
        return RenderResult::failed;
    }
    for (const auto& range : draw.ranges)
    {
        const auto* pipeline = dynamic_cast<const VulkanGraphicsPipeline*>(&range.pipeline);
        if (!pipeline || pipeline->owner != this || vertices->layout != pipeline->vertex_layout ||
            range.record_count == 0 || range.first_record > vertices->count ||
            range.record_count > vertices->count - range.first_record)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid draw range or resource ownership.");
            return RenderResult::failed;
        }
    }
    const auto* compute = draw.compute ? dynamic_cast<const VulkanComputePipeline*>(&draw.compute->pipeline) : nullptr;
    if (draw.compute && (!compute || compute->owner != this || compute->vertices != vertices ||
        draw.compute->group_count == 0 || draw.compute->group_count > 65535 ||
        draw.compute->constants[0] != vertices->count))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid compute dispatch or resource ownership.");
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
    auto& frame = frames_[frame_index_];
    if (!check(vkWaitForFences(device_, 1, &frame.submitted, VK_TRUE,
        std::numeric_limits<std::uint64_t>::max()), "Wait for frame command buffer") || !read_timestamps(frame_index_))
    {
        return RenderResult::failed;
    }
    std::uint32_t image_index = 0;
    const VkResult acquired = vkAcquireNextImageKHR(device_, swapchain_,
        std::numeric_limits<std::uint64_t>::max(), frame.acquired, VK_NULL_HANDLE, &image_index);
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
    if (!check(vkResetCommandPool(device_, frame.pool, 0), "Reset frame command pool"))
    {
        return RenderResult::failed;
    }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!check(vkBeginCommandBuffer(frame.command, &begin), "Begin Vulkan command buffer"))
    {
        return RenderResult::failed;
    }
    const auto query = static_cast<std::uint32_t>(frame_index_) * 4;
    if (timestamps_)
    {
        vkCmdResetQueryPool(frame.command, timestamps_, query, 4);
        vkCmdWriteTimestamp(frame.command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamps_, query);
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
        vkCmdPipelineBarrier(frame.command, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &storage, 0, nullptr);
        vkCmdBindPipeline(frame.command, VK_PIPELINE_BIND_POINT_COMPUTE, compute->pipeline);
        vkCmdBindDescriptorSets(frame.command, VK_PIPELINE_BIND_POINT_COMPUTE, compute->layout,
            0, 1, &compute->descriptor, 0, nullptr);
        vkCmdPushConstants(frame.command, compute->layout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, 4 * sizeof(std::uint32_t), draw.compute->constants.data());
        vkCmdDispatch(frame.command, draw.compute->group_count, 1, 1);
        storage.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        storage.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        vkCmdPipelineBarrier(frame.command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr, 1, &storage, 0, nullptr);
    }
    if (timestamps_)
    {
        vkCmdWriteTimestamp(frame.command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamps_, query + 1);
        vkCmdWriteTimestamp(frame.command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamps_, query + 2);
    }
    VkClearValue clear{};
    std::copy(draw.clear_color.begin(), draw.clear_color.end(), clear.color.float32);
    VkRenderPassBeginInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = render_pass_;
    pass.framebuffer = image.framebuffer;
    pass.renderArea.extent = extent_;
    pass.clearValueCount = 1;
    pass.pClearValues = &clear;
    vkCmdBeginRenderPass(frame.command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{0.0f, static_cast<float>(extent_.height),
        static_cast<float>(extent_.width), -static_cast<float>(extent_.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, extent_};
    vkCmdSetViewport(frame.command, 0, 1, &viewport);
    vkCmdSetScissor(frame.command, 0, 1, &scissor);
    for (const auto& range : draw.ranges)
    {
        const auto& pipeline = static_cast<const VulkanGraphicsPipeline&>(range.pipeline);
        vkCmdBindPipeline(frame.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
        const VkDeviceSize offset = static_cast<VkDeviceSize>(range.first_record) * vertex_stride(vertices->layout);
        vkCmdBindVertexBuffers(frame.command, 0, 1, &vertices->buffer, &offset);
        if (pipeline.vertex_layout == VertexLayout::particle)
        {
            vkCmdPushConstants(frame.command, pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float), &draw.aspect_ratio);
            vkCmdPushConstants(frame.command, pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(float), sizeof(std::uint32_t), &range.first_record);
            vkCmdDraw(frame.command, 6, range.record_count, 0, 0);
        }
        else vkCmdDraw(frame.command, range.record_count, 1, 0, 0);
    }
    vkCmdEndRenderPass(frame.command);
    if (timestamps_) vkCmdWriteTimestamp(frame.command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamps_, query + 3);
    if (!check(vkEndCommandBuffer(frame.command), "End Vulkan command buffer") ||
        !check(vkResetFences(device_, 1, &frame.submitted), "Reset frame fence") ||
        !check(vkResetFences(device_, 1, &image.presented), "Reset presentation fence"))
    {
        return RenderResult::failed;
    }
    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &frame.acquired;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &frame.command;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &image.rendered;
    if (!check(vkQueueSubmit(queue_, 1, &submit, frame.submitted), "Submit Vulkan frame"))
    {
        return RenderResult::failed;
    }
    frame.timestamps_pending = timestamps_ != VK_NULL_HANDLE;
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
