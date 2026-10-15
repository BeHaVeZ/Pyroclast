#include "vulkan_renderer.hpp"

#include <SDL3/SDL.h>

#include <cstring>
#include <utility>

namespace pyroclast
{
bool VulkanRenderer::create_capture_buffer(VkDeviceSize size)
{
    if (capture_.buffer && !wait_idle())
    {
        return false;
    }
    capture_.buffer.reset();
    capture_.memory.reset();
    capture_.size = 0;
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!check(vkCreateBuffer(vk_.device, &info, nullptr, capture_.buffer.put(vk_.device)), "Create capture readback buffer"))
    {
        return false;
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(vk_.device, capture_.buffer, &requirements);
    const auto type = find_memory_type(vk_.physical, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    if (!type)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Vulkan adapter lacks host-visible coherent readback memory.");
        return false;
    }
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = *type;
    if (!check(vkAllocateMemory(vk_.device, &allocation, nullptr, capture_.memory.put(vk_.device)), "Allocate capture readback memory") ||
        !check(vkBindBufferMemory(vk_.device, capture_.buffer, capture_.memory, 0), "Bind capture readback memory"))
    {
        return false;
    }
    capture_.size = size;
    return validation_ok();
}

bool VulkanRenderer::prepare_capture()
{
    const VkDeviceSize size = static_cast<VkDeviceSize>(swapchain_.extent.width) * swapchain_.extent.height * 4;
    return capture_.size == size || create_capture_buffer(size);
}

void VulkanRenderer::record_capture(VkCommandBuffer command, VkImage image)
{
    transition_image(command, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {swapchain_.extent.width, swapchain_.extent.height, 1};
    vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, capture_.buffer, 1, &region);
    transition_image(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
    transition_buffer(command, capture_.buffer, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    capture_.extent = swapchain_.extent;
    capture_.pending = true;
}

std::optional<CapturedImage> VulkanRenderer::read_capture()
{
    if (!capture_.pending)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No submitted frame requested a capture.");
        return std::nullopt;
    }
    if (!wait_idle())
    {
        return std::nullopt;
    }
    capture_.pending = false;
    void* mapped = nullptr;
    if (!check(vkMapMemory(vk_.device, capture_.memory, 0, capture_.size, 0, &mapped), "Map capture readback memory"))
    {
        return std::nullopt;
    }
    CapturedImage image;
    image.width = capture_.extent.width;
    image.height = capture_.extent.height;
    image.rgba.resize(static_cast<std::size_t>(capture_.size));
    std::memcpy(image.rgba.data(), mapped, image.rgba.size());
    vkUnmapMemory(vk_.device, capture_.memory);
    const bool bgra = format_ == VK_FORMAT_B8G8R8A8_UNORM;
    for (std::size_t pixel = 0; pixel < image.rgba.size(); pixel += 4)
    {
        if (bgra) std::swap(image.rgba[pixel], image.rgba[pixel + 2]);
        image.rgba[pixel + 3] = 255;
    }
    return image;
}
}
