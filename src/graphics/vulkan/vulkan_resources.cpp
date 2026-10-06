#include "vulkan_resources.hpp"

#include <SDL3/SDL.h>

#include <cstring>
#include <limits>

namespace pyroclast
{
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

std::unique_ptr<VertexBuffer> VulkanRenderer::create_vertex_buffer(std::span<const std::byte> vertices, VertexLayout layout)
{
    if (vertices.empty() || vertices.size() % vertex_stride(layout) != 0 || vertices.size() > std::numeric_limits<std::uint32_t>::max())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Vertex buffer requires a nonempty, bounded vertex span.");
        return nullptr;
    }
    auto buffer = std::make_unique<VulkanVertexBuffer>();
    buffer->owner = this;
    buffer->device = device_;
    buffer->count = static_cast<std::uint32_t>(vertices.size() / vertex_stride(layout));
    buffer->layout = layout;
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = vertices.size_bytes();
    info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!check(vkCreateBuffer(device_, &info, nullptr, &buffer->buffer), "Create Vulkan vertex buffer"))
    {
        return nullptr;
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, buffer->buffer, &requirements);
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &properties);
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
    if (!check(vkAllocateMemory(device_, &allocation, nullptr, &buffer->memory), "Allocate Vulkan vertex memory") ||
        !check(vkBindBufferMemory(device_, buffer->buffer, buffer->memory, 0), "Bind Vulkan vertex memory"))
    {
        return nullptr;
    }
    void* mapped = nullptr;
    if (!check(vkMapMemory(device_, buffer->memory, 0, info.size, 0, &mapped), "Map Vulkan vertex memory"))
    {
        return nullptr;
    }
    std::memcpy(mapped, vertices.data(), vertices.size_bytes());
    vkUnmapMemory(device_, buffer->memory);
    SDL_Log("Vertex buffer uploaded: %u vertices.", buffer->count);
    return validation_ok() ? std::move(buffer) : nullptr;
}

std::unique_ptr<GraphicsPipeline> VulkanRenderer::create_graphics_pipeline(
    const GraphicsPipelineDescription& shaders)
{
    const auto valid = [](std::span<const std::byte> bytes) {
        if (bytes.size() < 5 * sizeof(std::uint32_t) || bytes.size() % sizeof(std::uint32_t) != 0)
        {
            return false;
        }
        std::uint32_t magic = 0;
        std::memcpy(&magic, bytes.data(), sizeof(magic));
        return magic == 0x07230203;
    };
    if (!valid(shaders.vertex_shader) || !valid(shaders.pixel_shader))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Vulkan graphics pipeline requires word-sized SPIR-V shaders.");
        return nullptr;
    }
    struct Modules
    {
        VkDevice device;
        VkShaderModule vertex = VK_NULL_HANDLE;
        VkShaderModule pixel = VK_NULL_HANDLE;
        ~Modules()
        {
            vkDestroyShaderModule(device, vertex, nullptr);
            vkDestroyShaderModule(device, pixel, nullptr);
        }
    } modules{device_};
    const auto create_module = [this](std::span<const std::byte> bytes, VkShaderModule& module) {
        std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
        std::memcpy(words.data(), bytes.data(), bytes.size());
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = bytes.size();
        info.pCode = words.data();
        return check(vkCreateShaderModule(device_, &info, nullptr, &module), "Create Vulkan shader module");
    };
    if (!create_module(shaders.vertex_shader, modules.vertex) || !create_module(shaders.pixel_shader, modules.pixel))
    {
        return nullptr;
    }
    auto pipeline = std::make_unique<VulkanGraphicsPipeline>();
    pipeline->device = device_;
    pipeline->owner = this;
    pipeline->vertex_layout = shaders.layout;
    pipeline->blend = shaders.blend;
    const bool particles = shaders.layout == VertexLayout::particle;
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    const VkPushConstantRange aspect{VK_SHADER_STAGE_VERTEX_BIT, 0, 2 * sizeof(std::uint32_t)};
    layout.pushConstantRangeCount = particles ? 1 : 0;
    layout.pPushConstantRanges = particles ? &aspect : nullptr;
    if (!check(vkCreatePipelineLayout(device_, &layout, nullptr, &pipeline->layout), "Create Vulkan pipeline layout"))
    {
        return nullptr;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = modules.vertex;
    stages[0].pName = "vs_main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = modules.pixel;
    stages[1].pName = "ps_main";
    const VkVertexInputBindingDescription binding{0, vertex_stride(shaders.layout),
        particles ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)}};
    const VkVertexInputAttributeDescription particle_attributes[] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 16}};
    VkPipelineVertexInputStateCreateInfo input{};
    input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = 2;
    input.pVertexAttributeDescriptions = particles ? particle_attributes : attributes;
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
    raster.cullMode = particles ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
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
    info.renderPass = render_pass_;
    if (!check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline->pipeline), "Create Vulkan graphics pipeline"))
    {
        return nullptr;
    }
    return validation_ok() ? std::move(pipeline) : nullptr;
}
}
