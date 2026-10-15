#include "vulkan_resources.hpp"

#include <SDL3/SDL.h>

namespace pyroclast
{
VulkanComputePipeline::~VulkanComputePipeline()
{
    vkDestroyPipeline(device, pipeline, nullptr);
}

std::unique_ptr<ComputePipeline> VulkanRenderer::create_compute_pipeline(Shader shader)
{
    auto pipeline = std::make_unique<VulkanComputePipeline>(this);
    pipeline->device = vk_.device;
    VulkanShaderModule module;
    if (!create_shader_module(shader, module))
    {
        return nullptr;
    }
    VkComputePipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = module.handle;
    info.stage.pName = module.entry;
    info.layout = bindings_.layout;
    if (!check(vkCreateComputePipelines(vk_.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline->pipeline), "Create compute pipeline"))
    {
        return nullptr;
    }
    return validation_ok() ? std::move(pipeline) : nullptr;
}
}
