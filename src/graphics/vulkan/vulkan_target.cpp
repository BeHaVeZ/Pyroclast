#include "vulkan_renderer.hpp"
#include "vulkan_resources.hpp"

#include <SDL3/SDL.h>

namespace pyroclast
{
VulkanRenderTarget::~VulkanRenderTarget()
{
    vkDestroyImageView(device, view, nullptr);
    vkDestroyImage(device, image, nullptr);
    vkFreeMemory(device, memory, nullptr);
}

std::unique_ptr<RenderTarget> VulkanRenderer::create_render_target(const RenderTargetDescription& description)
{
    if (description.divisor == 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Render target divisor must be at least one.");
        return nullptr;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(vk_.physical, format_, &properties);
    constexpr auto required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((properties.optimalTilingFeatures & required) != required)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Vulkan color format cannot be rendered to and linearly sampled.");
        return nullptr;
    }
    auto target = std::make_unique<VulkanRenderTarget>(description);
    target->owner = this;
    target->device = vk_.device;
    return validation_ok() ? std::move(target) : nullptr;
}

bool VulkanRenderer::size_target(VulkanRenderTarget& target)
{
    const auto divisor = target.description().divisor;
    const VkExtent2D extent{(swapchain_.extent.width + divisor - 1) / divisor, (swapchain_.extent.height + divisor - 1) / divisor};
    if (target.image && target.extent.width == extent.width && target.extent.height == extent.height)
    {
        return true;
    }
    if (!wait_idle())
    {
        return false;
    }
    vkDestroyImageView(vk_.device, target.view, nullptr);
    vkDestroyImage(vk_.device, target.image, nullptr);
    vkFreeMemory(vk_.device, target.memory, nullptr);
    target.view = VK_NULL_HANDLE;
    target.image = VK_NULL_HANDLE;
    target.memory = VK_NULL_HANDLE;

    VkImageCreateInfo texture{};
    texture.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    texture.imageType = VK_IMAGE_TYPE_2D;
    texture.format = format_;
    texture.extent = {extent.width, extent.height, 1};
    texture.mipLevels = texture.arrayLayers = 1;
    texture.samples = VK_SAMPLE_COUNT_1_BIT;
    texture.tiling = VK_IMAGE_TILING_OPTIMAL;
    texture.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!check(vkCreateImage(vk_.device, &texture, nullptr, &target.image), "Create render target image")) return false;
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(vk_.device, target.image, &requirements);
    const auto type = find_memory_type(vk_.physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    if (!type)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Vulkan adapter lacks device-local render target memory.");
        return false;
    }
    VkMemoryAllocateInfo memory{};
    memory.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memory.allocationSize = requirements.size;
    memory.memoryTypeIndex = *type;
    if (!check(vkAllocateMemory(vk_.device, &memory, nullptr, &target.memory), "Allocate render target memory") ||
        !check(vkBindImageMemory(vk_.device, target.image, target.memory, 0), "Bind render target memory")) return false;
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = target.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format_;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!check(vkCreateImageView(vk_.device, &view, nullptr, &target.view), "Create render target view")) return false;
    target.extent = extent;
    SDL_Log("Vulkan render target: %ux%u, UNORM8.", extent.width, extent.height);
    return validation_ok();
}
}
