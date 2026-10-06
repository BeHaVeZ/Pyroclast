#include "vulkan_renderer.hpp"
#include "vulkan_resources.hpp"

#include <SDL3/SDL.h>
#include <algorithm>

namespace pyroclast
{
bool VulkanRenderer::create_smoke_pipeline()
{
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(physical_, format_, &properties);
    constexpr auto required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((properties.optimalTilingFeatures & required) != required)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Vulkan color format cannot render and linearly sample smoke.");
        return false;
    }
    VkAttachmentDescription attachment{};
    attachment.format = format_;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    const VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass.attachmentCount = 1;
    pass.pAttachments = &attachment;
    pass.subpassCount = 1;
    pass.pSubpasses = &subpass;
    pass.dependencyCount = 1;
    pass.pDependencies = &dependency;
    if (!check(vkCreateRenderPass(device_, &pass, nullptr, &smoke_pass_), "Create smoke render pass")) return false;

    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (!check(vkCreateSampler(device_, &sampler, nullptr, &smoke_sampler_), "Create smoke sampler")) return false;
    const VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &smoke_sampler_}};
    VkDescriptorSetLayoutCreateInfo descriptors{};
    descriptors.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptors.bindingCount = 2;
    descriptors.pBindings = bindings;
    if (!check(vkCreateDescriptorSetLayout(device_, &descriptors, nullptr, &smoke_descriptors_), "Create smoke descriptors")) return false;
    const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1}, {VK_DESCRIPTOR_TYPE_SAMPLER, 1}};
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 1;
    pool.poolSizeCount = 2;
    pool.pPoolSizes = sizes;
    if (!check(vkCreateDescriptorPool(device_, &pool, nullptr, &smoke_pool_), "Create smoke descriptor pool")) return false;
    VkDescriptorSetAllocateInfo set{};
    set.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set.descriptorPool = smoke_pool_;
    set.descriptorSetCount = 1;
    set.pSetLayouts = &smoke_descriptors_;
    if (!check(vkAllocateDescriptorSets(device_, &set, &smoke_set_), "Allocate smoke descriptor")) return false;
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &smoke_descriptors_;
    if (!check(vkCreatePipelineLayout(device_, &layout, nullptr, &smoke_layout_), "Create smoke composite layout")) return false;

    VulkanShaderModule vertex;
    VulkanShaderModule pixel;
    if (!create_shader_module(Shader::smoke_composite_vertex, vertex) ||
        !create_shader_module(Shader::smoke_composite_pixel, pixel)) return false;
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex.handle;
    stages[0].pName = "vs_main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = pixel.handle;
    stages[1].pName = "ps_main";
    VkPipelineVertexInputStateCreateInfo input{};
    input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo samples{};
    samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState opaque{};
    opaque.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &opaque;
    const VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = states;
    VkGraphicsPipelineCreateInfo pipeline{};
    pipeline.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline.stageCount = 2;
    pipeline.pStages = stages;
    pipeline.pVertexInputState = &input;
    pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &viewport;
    pipeline.pRasterizationState = &raster;
    pipeline.pMultisampleState = &samples;
    pipeline.pColorBlendState = &blend;
    pipeline.pDynamicState = &dynamic;
    pipeline.layout = smoke_layout_;
    pipeline.renderPass = render_pass_;
    return check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline, nullptr, &smoke_composite_), "Create smoke composite pipeline");
}

bool VulkanRenderer::create_smoke_target()
{
    smoke_extent_ = {(extent_.width + 1) / 2, (extent_.height + 1) / 2};
    VkImageCreateInfo texture{};
    texture.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    texture.imageType = VK_IMAGE_TYPE_2D;
    texture.format = format_;
    texture.extent = {smoke_extent_.width, smoke_extent_.height, 1};
    texture.mipLevels = texture.arrayLayers = 1;
    texture.samples = VK_SAMPLE_COUNT_1_BIT;
    texture.tiling = VK_IMAGE_TILING_OPTIMAL;
    texture.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!check(vkCreateImage(device_, &texture, nullptr, &smoke_image_), "Create half-resolution smoke image")) return false;
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, smoke_image_, &requirements);
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &properties);
    std::uint32_t type = properties.memoryTypeCount;
    for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
    {
        if ((requirements.memoryTypeBits & (1u << index)) &&
            (properties.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        {
            type = index;
            break;
        }
    }
    if (type == properties.memoryTypeCount)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Vulkan adapter lacks device-local smoke image memory.");
        return false;
    }
    VkMemoryAllocateInfo memory{};
    memory.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memory.allocationSize = requirements.size;
    memory.memoryTypeIndex = type;
    if (!check(vkAllocateMemory(device_, &memory, nullptr, &smoke_memory_), "Allocate smoke image memory") ||
        !check(vkBindImageMemory(device_, smoke_image_, smoke_memory_, 0), "Bind smoke image memory")) return false;
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = smoke_image_;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format_;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!check(vkCreateImageView(device_, &view, nullptr, &smoke_view_), "Create smoke image view")) return false;
    VkFramebufferCreateInfo framebuffer{};
    framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer.renderPass = smoke_pass_;
    framebuffer.attachmentCount = 1;
    framebuffer.pAttachments = &smoke_view_;
    framebuffer.width = smoke_extent_.width;
    framebuffer.height = smoke_extent_.height;
    framebuffer.layers = 1;
    if (!check(vkCreateFramebuffer(device_, &framebuffer, nullptr, &smoke_framebuffer_), "Create smoke framebuffer")) return false;
    const VkDescriptorImageInfo image{VK_NULL_HANDLE, smoke_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = smoke_set_;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    SDL_Log("Vulkan smoke target: %ux%u, UNORM8; includes clear background.", smoke_extent_.width, smoke_extent_.height);
    return validation_ok();
}

void VulkanRenderer::destroy_smoke_target()
{
    vkDestroyFramebuffer(device_, smoke_framebuffer_, nullptr);
    vkDestroyImageView(device_, smoke_view_, nullptr);
    vkDestroyImage(device_, smoke_image_, nullptr);
    vkFreeMemory(device_, smoke_memory_, nullptr);
    smoke_framebuffer_ = VK_NULL_HANDLE;
    smoke_view_ = VK_NULL_HANDLE;
    smoke_image_ = VK_NULL_HANDLE;
    smoke_memory_ = VK_NULL_HANDLE;
}

void VulkanRenderer::set_viewport(VkCommandBuffer command, VkExtent2D extent)
{
    const VkViewport viewport{0, static_cast<float>(extent.height), static_cast<float>(extent.width),
        -static_cast<float>(extent.height), 0, 1};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
}

void VulkanRenderer::draw_range(VkCommandBuffer command, const DrawRange& range, const VertexBuffer& buffer, float aspect)
{
    const auto& vertices = static_cast<const VulkanVertexBuffer&>(buffer);
    const auto& pipeline = static_cast<const VulkanGraphicsPipeline&>(range.pipeline);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
    const VkDeviceSize offset = static_cast<VkDeviceSize>(range.first_record) * vertices.stride();
    vkCmdBindVertexBuffers(command, 0, 1, &vertices.buffer, &offset);
    vkCmdPushConstants(command, pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float), &aspect);
    vkCmdPushConstants(command, pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(float), sizeof(std::uint32_t), &range.layer);
    vkCmdDraw(command, 6, range.record_count, 0, 0);
}

void VulkanRenderer::render_smoke(VkCommandBuffer command, const DrawCommand& draw)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = smoke_image_;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkClearValue clear{};
    std::copy(draw.clear_color.begin(), draw.clear_color.end(), clear.color.float32);
    VkRenderPassBeginInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = smoke_pass_;
    pass.framebuffer = smoke_framebuffer_;
    pass.renderArea.extent = smoke_extent_;
    pass.clearValueCount = 1;
    pass.pClearValues = &clear;
    vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    set_viewport(command, smoke_extent_);
    draw_range(command, draw.ranges.front(), draw.vertices, draw.aspect_ratio);
    vkCmdEndRenderPass(command);
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
}
}
