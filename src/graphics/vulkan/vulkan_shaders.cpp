#include "vulkan_resources.hpp"

#include <cstring>
#include <span>

namespace
{
#include "vulkan/particles_vs.hpp"
#include "vulkan/particles_ps.hpp"
#include "vulkan/particles_compute_cs.hpp"
#include "vulkan/upscale_vs.hpp"
#include "vulkan/upscale_ps.hpp"

std::span<const unsigned char> spirv(pyroclast::Shader shader)
{
    using pyroclast::Shader;
    switch (shader)
    {
    case Shader::particle_vertex: return particles_vs;
    case Shader::particle_pixel: return particles_ps;
    case Shader::particle_simulation: return particles_compute_cs;
    case Shader::upscale_vertex: return upscale_vs;
    case Shader::upscale_pixel: return upscale_ps;
    }
    return {};
}
}

namespace pyroclast
{
VulkanShaderModule::~VulkanShaderModule()
{
    vkDestroyShaderModule(device, handle, nullptr);
}

bool VulkanRenderer::create_shader_module(Shader shader, VulkanShaderModule& module)
{
    const auto bytes = spirv(shader);
    std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
    std::memcpy(words.data(), bytes.data(), words.size() * sizeof(std::uint32_t));
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = words.size() * sizeof(std::uint32_t);
    info.pCode = words.data();
    module.device = device_;
    return check(vkCreateShaderModule(device_, &info, nullptr, &module.handle), "Create Vulkan shader module");
}
}
