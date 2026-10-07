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

VulkanVertexBuffer::~VulkanVertexBuffer()
{
    vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, memory, nullptr);
}

VulkanGraphicsPipeline::~VulkanGraphicsPipeline()
{
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, layout, nullptr);
}

std::unique_ptr<VertexBuffer> VulkanRenderer::create_vertex_buffer(std::span<const std::byte> vertices, std::uint32_t stride)
{
    if (vertices.empty() || stride == 0 || vertices.size() % stride != 0 || vertices.size() > std::numeric_limits<std::uint32_t>::max())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Vertex buffer requires a nonempty, bounded span of whole records.");
        return nullptr;
    }
    auto buffer = std::make_unique<VulkanVertexBuffer>(static_cast<std::uint32_t>(vertices.size() / stride), stride);
    buffer->owner = this;
    buffer->device = vk_.device;
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = vertices.size_bytes();
    info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!check(vkCreateBuffer(vk_.device, &info, nullptr, &buffer->buffer), "Create Vulkan vertex buffer"))
    {
        return nullptr;
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(vk_.device, buffer->buffer, &requirements);
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(vk_.physical, &properties);
    std::uint32_t memory_type = properties.memoryTypeCount;
    constexpr VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
    {
        if ((requirements.memoryTypeBits & (1u << index)) &&
            (properties.memoryTypes[index].propertyFlags & required) == required)
        {
            if (memory_type == properties.memoryTypeCount) memory_type = index;
            if (properties.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
            {
                memory_type = index;
                break;
            }
        }
    }
    if (memory_type == properties.memoryTypeCount)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Vulkan adapter lacks host-visible coherent vertex memory.");
        return nullptr;
    }
    const auto& memory = properties.memoryTypes[memory_type];
    SDL_Log("Vulkan vertex memory: type=%u, heap=%u, flags=0x%X, device-local=%s, bytes=%llu.",
        memory_type, memory.heapIndex, memory.propertyFlags,
        (memory.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? "yes" : "no",
        static_cast<unsigned long long>(requirements.size));
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memory_type;
    if (!check(vkAllocateMemory(vk_.device, &allocation, nullptr, &buffer->memory), "Allocate Vulkan vertex memory") ||
        !check(vkBindBufferMemory(vk_.device, buffer->buffer, buffer->memory, 0), "Bind Vulkan vertex memory"))
    {
        return nullptr;
    }
    void* mapped = nullptr;
    if (!check(vkMapMemory(vk_.device, buffer->memory, 0, info.size, 0, &mapped), "Map Vulkan vertex memory"))
    {
        return nullptr;
    }
    std::memcpy(mapped, vertices.data(), vertices.size_bytes());
    vkUnmapMemory(vk_.device, buffer->memory);
    SDL_Log("Vertex buffer uploaded: %u vertices.", buffer->record_count());
    return validation_ok() ? std::move(buffer) : nullptr;
}

std::unique_ptr<GraphicsPipeline> VulkanRenderer::create_graphics_pipeline(
    const GraphicsPipelineDescription& shaders)
{
    VulkanShaderModule vertex;
    VulkanShaderModule pixel;
    if (!create_shader_module(shaders.vertex_shader, vertex) || !create_shader_module(shaders.pixel_shader, pixel))
    {
        return nullptr;
    }
    auto pipeline = std::make_unique<VulkanGraphicsPipeline>(shaders.blend);
    pipeline->device = vk_.device;
    pipeline->owner = this;
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    const VkPushConstantRange view{VK_SHADER_STAGE_VERTEX_BIT, 0, 2 * sizeof(std::uint32_t)};
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &view;
    if (!check(vkCreatePipelineLayout(vk_.device, &layout, nullptr, &pipeline->layout), "Create Vulkan pipeline layout"))
    {
        return nullptr;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex.handle;
    stages[0].pName = vertex.entry;
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = pixel.handle;
    stages[1].pName = pixel.entry;
    const VkVertexInputBindingDescription binding{0, particle_layout::particle_stride, VK_VERTEX_INPUT_RATE_INSTANCE};
    const VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, particle_layout::particle_motion_offset},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, particle_layout::particle_life_offset}};
    VkPipelineVertexInputStateCreateInfo input{};
    input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = 2;
    input.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
    info.pDynamicState = &dynamic;
    info.layout = pipeline->layout;
    info.renderPass = passes_.backbuffer;
    if (!check(vkCreateGraphicsPipelines(vk_.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline->pipeline), "Create Vulkan graphics pipeline"))
    {
        return nullptr;
    }
    return validation_ok() ? std::move(pipeline) : nullptr;
}
}
