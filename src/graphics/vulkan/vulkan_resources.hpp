#pragma once

#include "vulkan_renderer.hpp"

namespace pyroclast
{
struct VulkanShaderModule
{
    VulkanShaderModule() = default;
    VulkanShaderModule(const VulkanShaderModule&) = delete;
    VulkanShaderModule& operator=(const VulkanShaderModule&) = delete;
    ~VulkanShaderModule();
    VkDevice device = VK_NULL_HANDLE;
    VkShaderModule handle = VK_NULL_HANDLE;
    const char* entry = nullptr;
};

class VulkanBuffer final : public Buffer
{
public:
    using Buffer::Buffer;
    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    ~VulkanBuffer() override;
    const VulkanRenderer* owner = nullptr;
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

class VulkanGraphicsPipeline final : public GraphicsPipeline
{
public:
    using GraphicsPipeline::GraphicsPipeline;
    VulkanGraphicsPipeline(const VulkanGraphicsPipeline&) = delete;
    VulkanGraphicsPipeline& operator=(const VulkanGraphicsPipeline&) = delete;
    ~VulkanGraphicsPipeline() override;
    const VulkanRenderer* owner = nullptr;
    VkDevice device = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

class VulkanRenderTarget final : public RenderTarget
{
public:
    using RenderTarget::RenderTarget;
    VulkanRenderTarget(const VulkanRenderTarget&) = delete;
    VulkanRenderTarget& operator=(const VulkanRenderTarget&) = delete;
    ~VulkanRenderTarget() override;
    const VulkanRenderer* owner = nullptr;
    VkDevice device = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkExtent2D extent{};
};

class VulkanComputePipeline final : public ComputePipeline
{
public:
    VulkanComputePipeline() = default;
    VulkanComputePipeline(const VulkanComputePipeline&) = delete;
    VulkanComputePipeline& operator=(const VulkanComputePipeline&) = delete;
    ~VulkanComputePipeline() override;
    const VulkanRenderer* owner = nullptr;
    VkDevice device = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
};
}
