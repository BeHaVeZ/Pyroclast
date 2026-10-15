#pragma once

#include <vulkan/vulkan.h>

#include <utility>

namespace pyroclast
{
// Owns one device-level Vulkan handle and destroys it with `Destroy`. Move-only.
template <typename Handle, auto Destroy>
class VkUnique
{
public:
    VkUnique() = default;
    VkUnique(const VkUnique&) = delete;
    VkUnique& operator=(const VkUnique&) = delete;
    VkUnique(VkUnique&& other) noexcept : device_(other.device_), handle_(std::exchange(other.handle_, VK_NULL_HANDLE)) {}
    VkUnique& operator=(VkUnique&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            device_ = other.device_;
            handle_ = std::exchange(other.handle_, VK_NULL_HANDLE);
        }
        return *this;
    }
    ~VkUnique() { reset(); }

    void reset() noexcept
    {
        if (handle_ != VK_NULL_HANDLE) Destroy(device_, std::exchange(handle_, VK_NULL_HANDLE), nullptr);
    }
    // Destroys the current handle and returns the slot a vkCreate* or vkAllocate* call fills.
    Handle* put(VkDevice device) noexcept
    {
        reset();
        device_ = device;
        return &handle_;
    }
    Handle get() const { return handle_; }
    operator Handle() const { return handle_; }
    const Handle* address() const { return &handle_; }
    explicit operator bool() const { return handle_ != VK_NULL_HANDLE; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    Handle handle_ = VK_NULL_HANDLE;
};

using UniqueBuffer = VkUnique<VkBuffer, vkDestroyBuffer>;
using UniqueMemory = VkUnique<VkDeviceMemory, vkFreeMemory>;
using UniqueImage = VkUnique<VkImage, vkDestroyImage>;
using UniqueImageView = VkUnique<VkImageView, vkDestroyImageView>;
using UniqueSemaphore = VkUnique<VkSemaphore, vkDestroySemaphore>;
using UniqueFence = VkUnique<VkFence, vkDestroyFence>;
using UniqueCommandPool = VkUnique<VkCommandPool, vkDestroyCommandPool>;
using UniqueQueryPool = VkUnique<VkQueryPool, vkDestroyQueryPool>;
using UniqueSampler = VkUnique<VkSampler, vkDestroySampler>;
using UniqueDescriptorSetLayout = VkUnique<VkDescriptorSetLayout, vkDestroyDescriptorSetLayout>;
using UniquePipelineLayout = VkUnique<VkPipelineLayout, vkDestroyPipelineLayout>;
using UniquePipeline = VkUnique<VkPipeline, vkDestroyPipeline>;
using UniqueShaderModule = VkUnique<VkShaderModule, vkDestroyShaderModule>;
using UniqueSwapchain = VkUnique<VkSwapchainKHR, vkDestroySwapchainKHR>;
}
