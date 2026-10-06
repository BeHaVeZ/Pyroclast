#pragma once

#include "graphics_device.hpp"

namespace pyroclast
{
GraphicsPipelineDescription triangle_shaders(Renderer renderer);
std::span<const std::byte> triangle_compute_shader(Renderer renderer);
}
