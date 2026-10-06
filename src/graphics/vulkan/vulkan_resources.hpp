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
};

class VulkanVertexBuffer final : public VertexBuffer
{
public:
    VulkanVertexBuffer() = default;
    VulkanVertexBuffer(const VulkanVertexBuffer&) = delete;
    VulkanVertexBuffer& operator=(const VulkanVertexBuffer&) = delete;
    ~VulkanVertexBuffer() override;
    std::uint32_t vertex_count() const override { return count; }
    const VulkanRenderer* owner = nullptr;
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    std::uint32_t count = 0;
    std::uint32_t stride = 0;
};

class VulkanGraphicsPipeline final : public GraphicsPipeline
{
public:
    VulkanGraphicsPipeline() = default;
    VulkanGraphicsPipeline(const VulkanGraphicsPipeline&) = delete;
    VulkanGraphicsPipeline& operator=(const VulkanGraphicsPipeline&) = delete;
    ~VulkanGraphicsPipeline() override;
    const VulkanRenderer* owner = nullptr;
    BlendMode blend = BlendMode::opaque;
    VkDevice device = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

class VulkanComputePipeline final : public ComputePipeline
{
public:
    VulkanComputePipeline() = default;
    VulkanComputePipeline(const VulkanComputePipeline&) = delete;
    VulkanComputePipeline& operator=(const VulkanComputePipeline&) = delete;
    ~VulkanComputePipeline() override;
    const VulkanRenderer* owner = nullptr;
    const VulkanVertexBuffer* vertices = nullptr;
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
};
}
