#pragma once

#include "graphics_device.hpp"

namespace pyroclast
{
GraphicsPipelineDescription particle_shaders(Renderer renderer);
std::span<const std::byte> particle_compute_shader(Renderer renderer);
}
