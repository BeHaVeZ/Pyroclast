#include "d3d12_shaders.hpp"

namespace
{
#include "d3d12_shaders.inl"
}

namespace pyroclast
{
std::span<const std::byte> d3d12_shader(Shader shader)
{
    switch (shader)
    {
#define PYROCLAST_SHADER(name, entry) case Shader::name: return std::as_bytes(std::span(name));
#include "shader_list.inl"
#undef PYROCLAST_SHADER
    }
    return {};
}
}
