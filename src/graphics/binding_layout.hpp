#pragma once

#include <cstdint>

namespace pyroclast::binding_layout
{
using uint = std::uint32_t;
#include "../../shaders/binding_layout.hlsli"

// Vulkan bindings of each slot class, as DXC assigns them.
inline constexpr uint frame_constants_binding = 1 + vulkan_b_shift;
inline constexpr uint read_buffer_binding = 0 + vulkan_t_shift;
inline constexpr uint texture_binding = read_buffer_slots + vulkan_t_shift;
inline constexpr uint write_buffer_binding = 0 + vulkan_u_shift;
inline constexpr uint sampler_binding = 0 + vulkan_s_shift;
inline constexpr uint sampler_count = 2;
static_assert(texture_binding + texture_slots <= write_buffer_binding &&
    write_buffer_binding + write_buffer_slots <= sampler_binding, "Vulkan binding classes overlap");
}
