#pragma once

#include "../shaders.hpp"

#include <cstddef>
#include <span>

namespace pyroclast
{
std::span<const std::byte> d3d12_shader(Shader shader);
}
