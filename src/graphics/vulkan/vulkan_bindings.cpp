#include "vulkan_renderer.hpp"
#include "vulkan_resources.hpp"

#include <SDL3/SDL.h>

#include <vector>

namespace pyroclast
{
namespace
{
constexpr VkShaderStageFlags all_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
}

bool VulkanRenderer::create_bindings()
{
    bindings_.push = reinterpret_cast<PFN_vkCmdPushDescriptorSetKHR>(vkGetDeviceProcAddr(vk_.device, "vkCmdPushDescriptorSetKHR"));
    if (!bindings_.push)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "vkCmdPushDescriptorSetKHR is unavailable.");
        return false;
    }
    const VkFilter filters[binding_layout::sampler_count] = {VK_FILTER_LINEAR, VK_FILTER_NEAREST};
    for (std::uint32_t index = 0; index < binding_layout::sampler_count; ++index)
    {
        VkSamplerCreateInfo sampler{};
        sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler.magFilter = sampler.minFilter = filters[index];
        sampler.mipmapMode = index == 0 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.maxLod = VK_LOD_CLAMP_NONE;
        if (!check(vkCreateSampler(vk_.device, &sampler, nullptr, bindings_.samplers[index].put(vk_.device)), "Create sampler")) return false;
    }
    std::vector<VkDescriptorSetLayoutBinding> slots;
    slots.push_back({binding_layout::frame_constants_binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, all_stages, nullptr});
    for (std::uint32_t slot = 0; slot < binding_layout::read_buffer_slots; ++slot)
    {
        slots.push_back({binding_layout::read_buffer_binding + slot, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, all_stages, nullptr});
    }
    for (std::uint32_t slot = 0; slot < binding_layout::texture_slots; ++slot)
    {
        slots.push_back({binding_layout::texture_binding + slot, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, all_stages, nullptr});
    }
    for (std::uint32_t slot = 0; slot < binding_layout::write_buffer_slots; ++slot)
    {
        slots.push_back({binding_layout::write_buffer_binding + slot, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, all_stages, nullptr});
    }
    for (std::uint32_t slot = 0; slot < binding_layout::sampler_count; ++slot)
    {
        slots.push_back({binding_layout::sampler_binding + slot, VK_DESCRIPTOR_TYPE_SAMPLER, 1, all_stages, nullptr});
    }
    VkPhysicalDevicePushDescriptorPropertiesKHR push_limits{};
    push_limits.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR;
    VkPhysicalDeviceProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &push_limits;
    vkGetPhysicalDeviceProperties2(vk_.physical, &properties);
    if (slots.size() > push_limits.maxPushDescriptors)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "The binding model needs %zu push descriptors; the device allows %u.",
            slots.size(), push_limits.maxPushDescriptors);
        return false;
    }
    VkDescriptorSetLayoutCreateInfo set{};
    set.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    set.bindingCount = static_cast<std::uint32_t>(slots.size());
    set.pBindings = slots.data();
    if (!check(vkCreateDescriptorSetLayout(vk_.device, &set, nullptr, bindings_.set_layout.put(vk_.device)), "Create descriptor set layout")) return false;
    const VkPushConstantRange constants{all_stages, 0, binding_layout::draw_constant_words * sizeof(std::uint32_t)};
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = bindings_.set_layout.address();
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &constants;
    if (!check(vkCreatePipelineLayout(vk_.device, &layout, nullptr, bindings_.layout.put(vk_.device)), "Create pipeline layout")) return false;

    for (auto& slot : frames_)
    {
        void* mapped = nullptr;
        if (!allocate_buffer(binding_layout::frame_constant_bytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                "Create frame constants", slot.constants, slot.constants_memory) ||
            !check(vkMapMemory(vk_.device, slot.constants_memory, 0, VK_WHOLE_SIZE, 0, &mapped), "Map frame constants"))
        {
            return false;
        }
        slot.mapped_constants = static_cast<std::byte*>(mapped);
        if (!allocate_buffer(max_readback_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                "Create frame readback", slot.readback, slot.readback_memory) ||
            !check(vkMapMemory(vk_.device, slot.readback_memory, 0, VK_WHOLE_SIZE, 0, &mapped), "Map frame readback"))
        {
            return false;
        }
        slot.mapped_readback = static_cast<const std::byte*>(mapped);
    }
    return validation_ok();
}

void VulkanRenderer::read_readback(VulkanFrameSlot& slot)
{
    if (!slot.readback_pending) return;
    readback_ = copy_readback(slot.mapped_readback, slot.readback_size, ++readback_sample_id_);
    slot.readback_pending = false;
}

void VulkanRenderer::push_bindings(VkCommandBuffer command, VkPipelineBindPoint point, const Bindings& bindings,
    const VulkanFrameSlot& slot, std::span<const RenderTarget* const> inputs)
{
    constexpr auto buffer_slots = 1 + binding_layout::read_buffer_slots + binding_layout::write_buffer_slots;
    std::array<VkWriteDescriptorSet, buffer_slots + binding_layout::texture_slots + binding_layout::sampler_count> writes{};
    std::array<VkDescriptorBufferInfo, buffer_slots> buffers{};
    std::array<VkDescriptorImageInfo, binding_layout::texture_slots> images{};
    std::array<VkDescriptorImageInfo, binding_layout::sampler_count> samplers{};
    std::uint32_t count = 0;
    std::uint32_t buffer_count = 0;
    const auto add_buffer = [&](std::uint32_t binding, VkDescriptorType type, VkBuffer buffer, VkDeviceSize range) {
        buffers[buffer_count] = {buffer, 0, range};
        auto& write = writes[count++];
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = type;
        write.pBufferInfo = &buffers[buffer_count++];
    };
    add_buffer(binding_layout::frame_constants_binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, slot.constants, binding_layout::frame_constant_bytes);
    for (std::uint32_t index = 0; index < bindings.read.size(); ++index)
    {
        if (!bindings.read[index]) continue;
        add_buffer(binding_layout::read_buffer_binding + index, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            static_cast<const VulkanBuffer*>(bindings.read[index])->buffer, VK_WHOLE_SIZE);
    }
    for (std::uint32_t index = 0; index < bindings.write.size(); ++index)
    {
        if (!bindings.write[index]) continue;
        add_buffer(binding_layout::write_buffer_binding + index, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            static_cast<const VulkanBuffer*>(bindings.write[index])->buffer, VK_WHOLE_SIZE);
    }
    for (std::uint32_t index = 0; index < inputs.size(); ++index)
    {
        if (!inputs[index]) continue;
        const auto* input = static_cast<const VulkanRenderTarget*>(inputs[index]);
        images[index] = {VK_NULL_HANDLE, input->view, input->description().format == TargetFormat::depth ?
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        auto& write = writes[count++];
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding_layout::texture_binding + index;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.pImageInfo = &images[index];
    }
    for (std::uint32_t index = 0; index < binding_layout::sampler_count; ++index)
    {
        samplers[index] = {bindings_.samplers[index], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
        auto& write = writes[count++];
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = binding_layout::sampler_binding + index;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        write.pImageInfo = &samplers[index];
    }
    bindings_.push(command, point, bindings_.layout, 0, count, writes.data());
}
}
