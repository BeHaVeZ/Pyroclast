#include "vulkan_renderer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <limits>

namespace pyroclast
{
bool VulkanRenderer::create_swapchain()
{
    VkSurfaceCapabilitiesKHR capabilities{};
    if (!check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_.physical, vk_.surface, &capabilities), "Query Vulkan surface capabilities"))
    {
        return false;
    }
    swapchain_.extent = capabilities.currentExtent;
    if (swapchain_.extent.width == std::numeric_limits<std::uint32_t>::max())
    {
        swapchain_.extent.width = std::clamp(static_cast<std::uint32_t>(width_),
            capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        swapchain_.extent.height = std::clamp(static_cast<std::uint32_t>(height_),
            capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }
    std::uint32_t count = 0;
    if (!check(vkGetPhysicalDeviceSurfaceFormatsKHR(vk_.physical, vk_.surface, &count, nullptr), "Enumerate Vulkan surface formats"))
    {
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(count);
    if (!check(vkGetPhysicalDeviceSurfaceFormatsKHR(vk_.physical, vk_.surface, &count, formats.data()), "Read Vulkan surface formats"))
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
    if (capture_enabled_ && !(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Vulkan surface images cannot be copied for --capture.");
        return false;
    }
    format_ = chosen->format;
    if (swapchain_.extent.width == 0 || swapchain_.extent.height == 0)
    {
        rebuild_ = true;
        return true;
    }
    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = vk_.surface;
    info.minImageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount != 0)
    {
        info.minImageCount = std::min(info.minImageCount, capabilities.maxImageCount);
    }
    info.imageFormat = format_;
    info.imageColorSpace = chosen->colorSpace;
    info.imageExtent = swapchain_.extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (capture_enabled_ ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
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
        if (!check(vkGetPhysicalDeviceSurfacePresentModesKHR(vk_.physical, vk_.surface, &mode_count, nullptr),
            "Query Vulkan present modes")) return false;
        std::vector<VkPresentModeKHR> modes(mode_count);
        if (!check(vkGetPhysicalDeviceSurfacePresentModesKHR(vk_.physical, vk_.surface, &mode_count, modes.data()),
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
    if (!check(vkCreateSwapchainKHR(vk_.device, &info, nullptr, &swapchain_.handle), "Create Vulkan swapchain") ||
        !check(vkGetSwapchainImagesKHR(vk_.device, swapchain_.handle, &count, nullptr), "Count Vulkan swapchain images"))
    {
        return false;
    }
    std::vector<VkImage> handles(count);
    if (!check(vkGetSwapchainImagesKHR(vk_.device, swapchain_.handle, &count, handles.data()), "Read Vulkan swapchain images"))
    {
        return false;
    }
    swapchain_.images.resize(count);
    for (std::size_t index = 0; index < swapchain_.images.size(); ++index)
    {
        auto& image = swapchain_.images[index];
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
        if (!check(vkCreateImageView(vk_.device, &view, nullptr, &image.view), "Create swapchain image view") ||
            !check(vkCreateSemaphore(vk_.device, &semaphore, nullptr, &image.rendered), "Create present-wait semaphore") ||
            (vk_.present_fences && !check(vkCreateFence(vk_.device, &fence, nullptr, &image.presented), "Create presentation fence")))
        {
            return false;
        }
    }
    rebuild_ = false;
    return validation_ok();
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
    swapchain_.reset();
    if (!create_swapchain())
    {
        return false;
    }
    if (swapchain_.handle)
    {
        SDL_Log("Vulkan swapchain resized: %ux%u", swapchain_.extent.width, swapchain_.extent.height);
    }
    return validation_ok();
}
}
