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

class VulkanVertexBuffer final : public VertexBuffer
{
public:
    using VertexBuffer::VertexBuffer;
    VulkanVertexBuffer(const VulkanVertexBuffer&) = delete;
    VulkanVertexBuffer& operator=(const VulkanVertexBuffer&) = delete;
    ~VulkanVertexBuffer() override;
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
    VkPipelineLayout layout = VK_NULL_HANDLE;
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
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkExtent2D extent{};
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
};

class VulkanComputePipeline final : public ComputePipeline
{
public:
    using ComputePipeline::ComputePipeline;
    VulkanComputePipeline(const VulkanComputePipeline&) = delete;
    VulkanComputePipeline& operator=(const VulkanComputePipeline&) = delete;
    ~VulkanComputePipeline() override;
    const VulkanRenderer* owner = nullptr;
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
};
}
