#include "vulkan_resources.hpp"

#include <cstring>
#include <span>

namespace
{
#include "vulkan_shaders.inl"

struct Program
{
    std::span<const unsigned char> spirv;
    const char* entry = nullptr;
};

Program program(pyroclast::Shader shader)
{
    using pyroclast::Shader;
    switch (shader)
    {
#define PYROCLAST_SHADER(name, entry) case Shader::name: return {name, entry};
#include "shader_list.inl"
#undef PYROCLAST_SHADER
    }
    return {};
}
}

namespace pyroclast
{
bool VulkanRenderer::create_shader_module(Shader shader, VulkanShaderModule& module)
{
    const auto [bytes, entry] = program(shader);
    std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
    std::memcpy(words.data(), bytes.data(), words.size() * sizeof(std::uint32_t));
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = words.size() * sizeof(std::uint32_t);
    info.pCode = words.data();
    module.entry = entry;
    return check(vkCreateShaderModule(vk_.device, &info, nullptr, module.handle.put(vk_.device)), "Create Vulkan shader module");
}
}
