#pragma once

namespace pyroclast
{
// Registered in PYROCLAST_SHADERS (cmake/Shaders.cmake), which generates shader_list.inl.
enum class Shader
{
#define PYROCLAST_SHADER(name, entry) name,
#include "shader_list.inl"
#undef PYROCLAST_SHADER
};
}
