#include "vulkan_renderer.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>

namespace pyroclast
{
bool VulkanRenderer::create_overlay()
{
    ImGui_ImplVulkan_InitInfo init{};
    init.ApiVersion = VK_API_VERSION_1_3;
    init.Instance = vk_.instance;
    init.PhysicalDevice = vk_.physical;
    init.Device = vk_.device;
    init.QueueFamily = vk_.queue_family;
    init.Queue = vk_.queue;
    init.DescriptorPoolSize = 16;
    init.MinImageCount = static_cast<std::uint32_t>(frames_.size());
    init.ImageCount = static_cast<std::uint32_t>(frames_.size());
    init.UseDynamicRendering = true;
    auto& rendering = init.PipelineInfoMain.PipelineRenderingCreateInfo;
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format_;
    init.CheckVkResultFn = [](VkResult result) {
        if (result != VK_SUCCESS) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Dear ImGui Vulkan renderer: VkResult %d", static_cast<int>(result));
    };
    if (!ImGui_ImplVulkan_Init(&init))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Dear ImGui Vulkan renderer failed to initialize.");
        return false;
    }
    overlay_ = true;
    return validation_ok();
}

void VulkanRenderer::destroy_overlay()
{
    if (!overlay_) return;
    ImGui_ImplVulkan_Shutdown();
    overlay_ = false;
}

void VulkanRenderer::record_overlay(VkCommandBuffer command, ImDrawData* overlay, VkImageView backbuffer, VkExtent2D extent)
{
    memory_barrier(command, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = backbuffer;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent = extent;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    vkCmdBeginRendering(command, &rendering);
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplVulkan_RenderDrawData(overlay, command);
    vkCmdEndRendering(command);
}
}
