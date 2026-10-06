#include "vulkan_renderer.hpp"
#include "vulkan_resources.hpp"

#include <SDL3/SDL.h>

namespace pyroclast
{
VulkanRenderTarget::~VulkanRenderTarget()
{
    vkDestroyFramebuffer(device, framebuffer, nullptr);
    vkDestroyImageView(device, view, nullptr);
    vkDestroyImage(device, image, nullptr);
    vkFreeMemory(device, memory, nullptr);
    vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
}

bool VulkanRenderer::create_upscale_pipeline()
{
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(physical_, format_, &properties);
    constexpr auto required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((properties.optimalTilingFeatures & required) != required)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Vulkan color format cannot be rendered to and linearly sampled.");
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
    if (!check(vkCreateRenderPass(device_, &pass, nullptr, &offscreen_pass_), "Create offscreen render pass")) return false;

    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (!check(vkCreateSampler(device_, &sampler, nullptr, &upscale_sampler_), "Create upscale sampler")) return false;
    const VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &upscale_sampler_}};
    VkDescriptorSetLayoutCreateInfo descriptors{};
    descriptors.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptors.bindingCount = 2;
    descriptors.pBindings = bindings;
    if (!check(vkCreateDescriptorSetLayout(device_, &descriptors, nullptr, &upscale_descriptors_), "Create upscale descriptors")) return false;
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &upscale_descriptors_;
    if (!check(vkCreatePipelineLayout(device_, &layout, nullptr, &upscale_layout_), "Create upscale layout")) return false;

    VulkanShaderModule vertex;
    VulkanShaderModule pixel;
    if (!create_shader_module(Shader::upscale_vertex, vertex) ||
        !create_shader_module(Shader::upscale_pixel, pixel)) return false;
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
    pipeline.layout = upscale_layout_;
    pipeline.renderPass = render_pass_;
    return check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline, nullptr, &upscale_pipeline_), "Create upscale pipeline");
}

std::unique_ptr<RenderTarget> VulkanRenderer::create_render_target(const RenderTargetDescription& description)
{
    if (description.divisor == 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Render target divisor must be at least one.");
        return nullptr;
    }
    if (!upscale_pipeline_ && !create_upscale_pipeline()) return nullptr;
    auto target = std::make_unique<VulkanRenderTarget>(description);
    target->owner = this;
    target->device = device_;
    const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1}, {VK_DESCRIPTOR_TYPE_SAMPLER, 1}};
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 1;
    pool.poolSizeCount = 2;
    pool.pPoolSizes = sizes;
    if (!check(vkCreateDescriptorPool(device_, &pool, nullptr, &target->descriptor_pool), "Create render target descriptor pool")) return nullptr;
    VkDescriptorSetAllocateInfo set{};
    set.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set.descriptorPool = target->descriptor_pool;
    set.descriptorSetCount = 1;
    set.pSetLayouts = &upscale_descriptors_;
    if (!check(vkAllocateDescriptorSets(device_, &set, &target->descriptor), "Allocate render target descriptor")) return nullptr;
    return validation_ok() ? std::move(target) : nullptr;
}

bool VulkanRenderer::size_target(VulkanRenderTarget& target)
{
    const auto divisor = target.description().divisor;
    const VkExtent2D extent{(extent_.width + divisor - 1) / divisor, (extent_.height + divisor - 1) / divisor};
    if (target.image && target.extent.width == extent.width && target.extent.height == extent.height)
    {
        return true;
    }
    if (!wait_idle())
    {
        return false;
    }
    vkDestroyFramebuffer(device_, target.framebuffer, nullptr);
    vkDestroyImageView(device_, target.view, nullptr);
    vkDestroyImage(device_, target.image, nullptr);
    vkFreeMemory(device_, target.memory, nullptr);
    target.framebuffer = VK_NULL_HANDLE;
    target.view = VK_NULL_HANDLE;
    target.image = VK_NULL_HANDLE;
    target.memory = VK_NULL_HANDLE;

    VkImageCreateInfo texture{};
    texture.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    texture.imageType = VK_IMAGE_TYPE_2D;
    texture.format = format_;
    texture.extent = {extent.width, extent.height, 1};
    texture.mipLevels = texture.arrayLayers = 1;
    texture.samples = VK_SAMPLE_COUNT_1_BIT;
    texture.tiling = VK_IMAGE_TILING_OPTIMAL;
    texture.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!check(vkCreateImage(device_, &texture, nullptr, &target.image), "Create render target image")) return false;
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, target.image, &requirements);
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
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Vulkan adapter lacks device-local render target memory.");
        return false;
    }
    VkMemoryAllocateInfo memory{};
    memory.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memory.allocationSize = requirements.size;
    memory.memoryTypeIndex = type;
    if (!check(vkAllocateMemory(device_, &memory, nullptr, &target.memory), "Allocate render target memory") ||
        !check(vkBindImageMemory(device_, target.image, target.memory, 0), "Bind render target memory")) return false;
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = target.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format_;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!check(vkCreateImageView(device_, &view, nullptr, &target.view), "Create render target view")) return false;
    VkFramebufferCreateInfo framebuffer{};
    framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer.renderPass = offscreen_pass_;
    framebuffer.attachmentCount = 1;
    framebuffer.pAttachments = &target.view;
    framebuffer.width = extent.width;
    framebuffer.height = extent.height;
    framebuffer.layers = 1;
    if (!check(vkCreateFramebuffer(device_, &framebuffer, nullptr, &target.framebuffer), "Create render target framebuffer")) return false;
    const VkDescriptorImageInfo image{VK_NULL_HANDLE, target.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = target.descriptor;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    target.extent = extent;
    SDL_Log("Vulkan render target: %ux%u, UNORM8.", extent.width, extent.height);
    return validation_ok();
}

void VulkanRenderer::upscale(VkCommandBuffer command, const VulkanRenderTarget& source)
{
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, upscale_pipeline_);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, upscale_layout_, 0, 1, &source.descriptor, 0, nullptr);
    vkCmdDraw(command, 3, 1, 0, 0);
}
}
