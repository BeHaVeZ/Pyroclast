#include "vulkan_resources.hpp"

#include <SDL3/SDL.h>

#include <cstring>
#include <limits>

namespace pyroclast
{
std::optional<std::uint32_t> find_memory_type(VkPhysicalDevice physical, std::uint32_t type_bits,
    VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred)
{
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    std::optional<std::uint32_t> found;
    for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
    {
        const auto flags = properties.memoryTypes[index].propertyFlags;
        if (!(type_bits & (1u << index)) || (flags & required) != required) continue;
        if ((flags & preferred) == preferred) return index;
        if (!found) found = index;
    }
    return found;
}

VulkanBuffer::~VulkanBuffer()
{
    vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, memory, nullptr);
}

VulkanGraphicsPipeline::~VulkanGraphicsPipeline()
{
    vkDestroyPipeline(device, pipeline, nullptr);
}

template <typename Record>
bool VulkanRenderer::submit_immediately(const char* operation, Record&& record)
{
    auto& slot = frames_[0];
    if (!wait_idle() || !check(vkResetCommandPool(vk_.device, slot.pool, 0), operation))
    {
        return false;
    }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!check(vkBeginCommandBuffer(slot.command, &begin), operation))
    {
        return false;
    }
    record(slot.command);
    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(slot.command, &dependency);
    VkCommandBufferSubmitInfo buffer{};
    buffer.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    buffer.commandBuffer = slot.command;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &buffer;
    return check(vkEndCommandBuffer(slot.command), operation) &&
        check(vkQueueSubmit2(vk_.queue, 1, &submit, VK_NULL_HANDLE), operation) &&
        check(vkQueueWaitIdle(vk_.queue), operation) && validation_ok();
}

bool VulkanRenderer::allocate_buffer(VulkanBuffer& buffer, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred, const char* purpose)
{
    buffer.owner = this;
    buffer.device = vk_.device;
    return allocate_buffer(buffer.size(), usage, required, preferred, purpose, buffer.buffer, buffer.memory);
}

bool VulkanRenderer::allocate_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags required,
    VkMemoryPropertyFlags preferred, const char* purpose, VkBuffer& buffer, VkDeviceMemory& memory)
{
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!check(vkCreateBuffer(vk_.device, &info, nullptr, &buffer), purpose))
    {
        return false;
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(vk_.device, buffer, &requirements);
    const auto type = find_memory_type(vk_.physical, requirements.memoryTypeBits, required, preferred);
    if (!type)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: no compatible Vulkan memory type.", purpose);
        return false;
    }
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(vk_.physical, &properties);
    SDL_Log("%s: memory type=%u, heap=%u, flags=0x%X, bytes=%llu.", purpose, *type, properties.memoryTypes[*type].heapIndex,
        properties.memoryTypes[*type].propertyFlags, static_cast<unsigned long long>(requirements.size));
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = *type;
    return check(vkAllocateMemory(vk_.device, &allocation, nullptr, &memory), purpose) &&
        check(vkBindBufferMemory(vk_.device, buffer, memory, 0), purpose);
}

std::unique_ptr<Buffer> VulkanRenderer::create_buffer(std::uint32_t size, std::span<const std::byte> initial)
{
    if (size == 0 || size % 4 != 0 || (!initial.empty() && initial.size() != size))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Buffer size must be a nonzero multiple of four bytes and match its initial data.");
        return nullptr;
    }
    auto buffer = std::make_unique<VulkanBuffer>(size);
    if (!allocate_buffer(*buffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, "Create Vulkan buffer"))
    {
        return nullptr;
    }
    if (initial.empty())
    {
        if (!submit_immediately("Zero-fill Vulkan buffer", [&buffer](VkCommandBuffer command) {
            vkCmdFillBuffer(command, buffer->buffer, 0, VK_WHOLE_SIZE, 0);
        }))
        {
            return nullptr;
        }
        return std::move(buffer);
    }
    VulkanBuffer staging(size);
    void* mapped = nullptr;
    if (!allocate_buffer(staging, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, "Create Vulkan staging buffer") ||
        !check(vkMapMemory(vk_.device, staging.memory, 0, size, 0, &mapped), "Map Vulkan staging buffer"))
    {
        return nullptr;
    }
    std::memcpy(mapped, initial.data(), initial.size_bytes());
    vkUnmapMemory(vk_.device, staging.memory);
    const VkBufferCopy region{0, 0, size};
    if (!submit_immediately("Upload Vulkan buffer", [&](VkCommandBuffer command) {
        vkCmdCopyBuffer(command, staging.buffer, buffer->buffer, 1, &region);
    }))
    {
        return nullptr;
    }
    SDL_Log("Buffer uploaded: %u bytes.", size);
    return std::move(buffer);
}

std::unique_ptr<GraphicsPipeline> VulkanRenderer::create_graphics_pipeline(
    const GraphicsPipelineDescription& shaders)
{
    if (shaders.target == TargetFormat::depth)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "A graphics pipeline's colour target cannot be a depth format.");
        return nullptr;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(vk_.physical, vulkan_format(shaders.target), &properties);
    const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        (shaders.blend != BlendMode::opaque ? VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT : 0);
    if ((properties.optimalTilingFeatures & required) != required)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "The Vulkan adapter cannot render%s to this pipeline's colour format.",
            shaders.blend != BlendMode::opaque ? " and blend" : "");
        return nullptr;
    }
    VulkanShaderModule vertex;
    VulkanShaderModule pixel;
    if (!create_shader_module(shaders.vertex_shader, vertex) || !create_shader_module(shaders.pixel_shader, pixel))
    {
        return nullptr;
    }
    auto pipeline = std::make_unique<VulkanGraphicsPipeline>(shaders.blend, shaders.depth, shaders.target);
    pipeline->device = vk_.device;
    pipeline->owner = this;
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex.handle;
    stages[0].pName = vertex.entry;
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = pixel.handle;
    stages[1].pName = pixel.entry;
    VkPipelineVertexInputStateCreateInfo input{};
    input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = shaders.topology == Topology::lines ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo samples{};
    samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.blendEnable = shaders.blend != BlendMode::opaque;
    attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstColorBlendFactor = shaders.blend == BlendMode::premultiplied_alpha ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA :
        shaders.blend == BlendMode::additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ZERO;
    attachment.colorBlendOp = VK_BLEND_OP_ADD;
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstAlphaBlendFactor = attachment.dstColorBlendFactor;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = shaders.depth == DepthMode::test || shaders.depth == DepthMode::test_write;
    depth.depthWriteEnable = shaders.depth == DepthMode::test_write;
    depth.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
    const VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = states;
    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &samples;
    info.pColorBlendState = &blend;
    info.pDepthStencilState = &depth;
    info.pDynamicState = &dynamic;
    info.layout = bindings_.layout;
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    const VkFormat color_format = vulkan_format(shaders.target);
    rendering.pColorAttachmentFormats = &color_format;
    rendering.depthAttachmentFormat = shaders.depth != DepthMode::none ? depth_format : VK_FORMAT_UNDEFINED;
    info.pNext = &rendering;
    if (!check(vkCreateGraphicsPipelines(vk_.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline->pipeline), "Create Vulkan graphics pipeline"))
    {
        return nullptr;
    }
    return validation_ok() ? std::move(pipeline) : nullptr;
}
}
