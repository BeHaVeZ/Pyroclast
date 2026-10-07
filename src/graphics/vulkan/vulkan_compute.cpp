#include "vulkan_resources.hpp"

#include <SDL3/SDL.h>

namespace pyroclast
{
VulkanComputePipeline::~VulkanComputePipeline()
{
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, layout, nullptr);
    vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
}

std::unique_ptr<ComputePipeline> VulkanRenderer::create_compute_pipeline(
    Shader shader, const Buffer& resource)
{
    const auto* vertices = dynamic_cast<const VulkanBuffer*>(&resource);
    if (!vertices || vertices->owner != this)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Compute requires a buffer belonging to this Vulkan device.");
        return nullptr;
    }
    auto pipeline = std::make_unique<VulkanComputePipeline>(*vertices);
    pipeline->owner = this;
    pipeline->device = vk_.device;
    const VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo descriptors{};
    descriptors.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptors.bindingCount = 1;
    descriptors.pBindings = &binding;
    if (!check(vkCreateDescriptorSetLayout(vk_.device, &descriptors, nullptr, &pipeline->descriptor_layout), "Create compute descriptor layout"))
    {
        return nullptr;
    }
    const VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 1;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &pool_size;
    if (!check(vkCreateDescriptorPool(vk_.device, &pool, nullptr, &pipeline->descriptor_pool), "Create compute descriptor pool"))
    {
        return nullptr;
    }
    VkDescriptorSetAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocation.descriptorPool = pipeline->descriptor_pool;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &pipeline->descriptor_layout;
    if (!check(vkAllocateDescriptorSets(vk_.device, &allocation, &pipeline->descriptor), "Allocate compute descriptor"))
    {
        return nullptr;
    }
    const VkDescriptorBufferInfo buffer{vertices->buffer, 0, vertices->size()};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = pipeline->descriptor;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &buffer;
    vkUpdateDescriptorSets(vk_.device, 1, &write, 0, nullptr);
    const VkPushConstantRange constants{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4 * sizeof(std::uint32_t)};
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &pipeline->descriptor_layout;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &constants;
    if (!check(vkCreatePipelineLayout(vk_.device, &layout, nullptr, &pipeline->layout), "Create compute pipeline layout"))
    {
        return nullptr;
    }
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
    info.layout = pipeline->layout;
    if (!check(vkCreateComputePipelines(vk_.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline->pipeline), "Create compute pipeline"))
    {
        return nullptr;
    }
    return validation_ok() ? std::move(pipeline) : nullptr;
}
}
