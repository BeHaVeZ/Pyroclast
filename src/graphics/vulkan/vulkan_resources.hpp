#pragma once

#include "vulkan_renderer.hpp"

namespace pyroclast
{
struct VulkanShaderModule
{
    UniqueShaderModule handle;
    const char* entry = nullptr;
};

class VulkanBuffer final : public Buffer
{
public:
    using Buffer::Buffer;
    UniqueMemory memory;
    UniqueBuffer buffer;
};

class VulkanGraphicsPipeline final : public GraphicsPipeline
{
public:
    using GraphicsPipeline::GraphicsPipeline;
    UniquePipeline pipeline;
};

class VulkanRenderTarget final : public RenderTarget
{
public:
    using RenderTarget::RenderTarget;
    UniqueMemory memory;
    UniqueImage image;
    UniqueImageView view;
    VkExtent2D extent{};
};

class VulkanComputePipeline final : public ComputePipeline
{
public:
    using ComputePipeline::ComputePipeline;
    UniquePipeline pipeline;
};
}
