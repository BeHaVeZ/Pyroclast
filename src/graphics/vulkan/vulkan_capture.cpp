#include "vulkan_renderer.hpp"

#include <SDL3/SDL.h>

#include <cstring>
#include <utility>

namespace pyroclast
{
VulkanCapture::~VulkanCapture()
{
    if (!device) return;
    vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, memory, nullptr);
}

bool VulkanRenderer::create_capture_buffer(VkDeviceSize size)
{
    if (capture_.buffer && !wait_idle())
    {
        return false;
    }
    vkDestroyBuffer(vk_.device, capture_.buffer, nullptr);
    vkFreeMemory(vk_.device, capture_.memory, nullptr);
    capture_.buffer = VK_NULL_HANDLE;
    capture_.memory = VK_NULL_HANDLE;
    capture_.size = 0;
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!check(vkCreateBuffer(vk_.device, &info, nullptr, &capture_.buffer), "Create capture readback buffer"))
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
    if (!check(vkAllocateMemory(vk_.device, &allocation, nullptr, &capture_.memory), "Allocate capture readback memory") ||
        !check(vkBindBufferMemory(vk_.device, capture_.buffer, capture_.memory, 0), "Bind capture readback memory"))
    {
        return false;
    }
    capture_.size = size;
    return validation_ok();
}

bool VulkanRenderer::record_capture(VkCommandBuffer command, VkImage image)
{
    const VkDeviceSize size = static_cast<VkDeviceSize>(swapchain_.extent.width) * swapchain_.extent.height * 4;
    if (capture_.size != size && !create_capture_buffer(size))
    {
        return false;
    }
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {swapchain_.extent.width, swapchain_.extent.height, 1};
    vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, capture_.buffer, 1, &region);
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferMemoryBarrier host{};
    host.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = capture_.buffer;
    host.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, nullptr, 1, &host, 0, nullptr);
    capture_.extent = swapchain_.extent;
    capture_.pending = true;
    return true;
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
