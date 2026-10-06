#include "vulkan_renderer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <limits>

namespace pyroclast
{
bool VulkanRenderer::create_swapchain()
{
    VkSurfaceCapabilitiesKHR capabilities{};
    if (!check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &capabilities), "Query Vulkan surface capabilities"))
    {
        return false;
    }
    extent_ = capabilities.currentExtent;
    if (extent_.width == std::numeric_limits<std::uint32_t>::max())
    {
        extent_.width = std::clamp(static_cast<std::uint32_t>(width_),
            capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent_.height = std::clamp(static_cast<std::uint32_t>(height_),
            capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }
    std::uint32_t count = 0;
    if (!check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, nullptr), "Enumerate Vulkan surface formats"))
    {
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(count);
    if (!check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, formats.data()), "Read Vulkan surface formats"))
    {
        return false;
    }
    const auto chosen = std::find_if(formats.begin(), formats.end(), [this](const auto& format) {
        return format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
            (format_ == VK_FORMAT_UNDEFINED ?
                (format.format == VK_FORMAT_R8G8B8A8_UNORM || format.format == VK_FORMAT_B8G8R8A8_UNORM) :
                format.format == format_);
    });
    if (chosen == formats.end() || !(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Vulkan surface lacks a compatible UNORM color attachment format.");
        return false;
    }
    format_ = chosen->format;
    if (!render_pass_)
    {
        VkAttachmentDescription attachment{};
        attachment.format = format_;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        const VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo pass{};
        pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        pass.attachmentCount = 1;
        pass.pAttachments = &attachment;
        pass.subpassCount = 1;
        pass.pSubpasses = &subpass;
        pass.dependencyCount = 1;
        pass.pDependencies = &dependency;
        if (!check(vkCreateRenderPass(device_, &pass, nullptr, &render_pass_), "Create Vulkan render pass"))
        {
            return false;
        }
    }
    if (extent_.width == 0 || extent_.height == 0)
    {
        rebuild_ = true;
        return true;
    }
    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface_;
    info.minImageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount != 0)
    {
        info.minImageCount = std::min(info.minImageCount, capabilities.maxImageCount);
    }
    info.imageFormat = format_;
    info.imageColorSpace = chosen->colorSpace;
    info.imageExtent = extent_;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = capabilities.currentTransform;
    for (const auto mode : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
    {
        if (capabilities.supportedCompositeAlpha & mode)
        {
            info.compositeAlpha = mode;
            break;
        }
    }
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync_)
    {
        std::uint32_t mode_count = 0;
        if (!check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &mode_count, nullptr),
            "Query Vulkan present modes")) return false;
        std::vector<VkPresentModeKHR> modes(mode_count);
        if (!check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &mode_count, modes.data()),
            "Read Vulkan present modes")) return false;
        if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) == modes.end())
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fixed-state profiling requires Vulkan IMMEDIATE presentation.");
            return false;
        }
        info.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    SDL_Log("Vulkan presentation mode: %s.", vsync_ ? "FIFO" : "IMMEDIATE");
    info.clipped = VK_TRUE;
    if (!check(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_), "Create Vulkan swapchain") ||
        !check(vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr), "Count Vulkan swapchain images"))
    {
        return false;
    }
    std::vector<VkImage> handles(count);
    if (!check(vkGetSwapchainImagesKHR(device_, swapchain_, &count, handles.data()), "Read Vulkan swapchain images"))
    {
        return false;
    }
    images_.resize(count);
    for (std::size_t index = 0; index < images_.size(); ++index)
    {
        auto& image = images_[index];
        image.image = handles[index];
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = image.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format_;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkSemaphoreCreateInfo semaphore{};
        semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fence{};
        fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (!check(vkCreateImageView(device_, &view, nullptr, &image.view), "Create swapchain image view") ||
            !check(vkCreateSemaphore(device_, &semaphore, nullptr, &image.rendered), "Create present-wait semaphore") ||
            !check(vkCreateFence(device_, &fence, nullptr, &image.presented), "Create presentation fence"))
        {
            return false;
        }
        VkFramebufferCreateInfo framebuffer{};
        framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer.renderPass = render_pass_;
        framebuffer.attachmentCount = 1;
        framebuffer.pAttachments = &image.view;
        framebuffer.width = extent_.width;
        framebuffer.height = extent_.height;
        framebuffer.layers = 1;
        if (!check(vkCreateFramebuffer(device_, &framebuffer, nullptr, &image.framebuffer), "Create Vulkan framebuffer"))
        {
            return false;
        }
    }
    rebuild_ = false;
    if (half_smoke_ && ((!smoke_pass_ && !create_smoke_pipeline()) || !create_smoke_target())) return false;
    return validation_ok();
}

void VulkanRenderer::destroy_swapchain()
{
    destroy_smoke_target();
    for (auto& image : images_)
    {
        vkDestroyFramebuffer(device_, image.framebuffer, nullptr);
        vkDestroyImageView(device_, image.view, nullptr);
        vkDestroySemaphore(device_, image.rendered, nullptr);
        vkDestroyFence(device_, image.presented, nullptr);
    }
    images_.clear();
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
    extent_ = {};
}

bool VulkanRenderer::resize(int width, int height)
{
    if (width <= 0 || height <= 0 || (width == width_ && height == height_ && !rebuild_))
    {
        return true;
    }
    width_ = width;
    height_ = height;
    if (!wait_idle())
    {
        return false;
    }
    destroy_swapchain();
    if (!create_swapchain())
    {
        return false;
    }
    if (swapchain_)
    {
        SDL_Log("Vulkan swapchain resized: %ux%u", extent_.width, extent_.height);
    }
    return validation_ok();
}
}
