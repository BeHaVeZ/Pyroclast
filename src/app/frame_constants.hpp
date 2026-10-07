#pragma once

#include "../core/math.hpp"
#include "../graphics/particle_layout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pyroclast
{
struct FrameConstants
{
    float4x4 view_projection;
    float4x4 inverse_view_projection;
    float4 camera_position;
    float4 camera_right;
    float4 camera_up;
    float4 sun_direction;
    float4 sun_color;
    float4 sky_color;
    float4 horizon_color;
    float4 ground_color;
    float4 fog;
    float4 viewport;
    float4 exposure;
    float4 projection;
    std::array<float4, particle_layout::maximum_effects> effect_origins;
    std::array<float4, particle_layout::maximum_effects> effect_normals;
    std::array<std::array<std::uint32_t, 4>, particle_layout::maximum_effects> effect_states;
    std::array<std::array<std::uint32_t, 4>, particle_layout::maximum_effects / 4> effect_order;
    std::array<float4, particle_layout::maximum_lights> light_positions;
    std::array<float4, particle_layout::maximum_lights> light_colors;
    std::array<std::uint32_t, 4> light_state;
    std::array<std::uint32_t, 4> clock;
    float4 wind;
    float4 sun_billboard;
    float4 up_billboard;
};
static_assert(particle_layout::maximum_effects % 4 == 0);
static_assert(sizeof(float4x4) == 64 && sizeof(float4) == 16);
static_assert(offsetof(FrameConstants, inverse_view_projection) == 64 && offsetof(FrameConstants, camera_position) == 128 &&
    offsetof(FrameConstants, camera_right) == 144 && offsetof(FrameConstants, camera_up) == 160 &&
    offsetof(FrameConstants, sun_direction) == 176 && offsetof(FrameConstants, sun_color) == 192 &&
    offsetof(FrameConstants, sky_color) == 208 && offsetof(FrameConstants, horizon_color) == 224 &&
    offsetof(FrameConstants, ground_color) == 240 && offsetof(FrameConstants, fog) == 256 &&
    offsetof(FrameConstants, viewport) == 272 && offsetof(FrameConstants, exposure) == 288 &&
    offsetof(FrameConstants, projection) == 304 && offsetof(FrameConstants, effect_origins) == 320 &&
    offsetof(FrameConstants, effect_normals) == 320 + 16 * particle_layout::maximum_effects &&
    offsetof(FrameConstants, effect_states) == 320 + 32 * particle_layout::maximum_effects &&
    offsetof(FrameConstants, effect_order) == 320 + 48 * particle_layout::maximum_effects &&
    offsetof(FrameConstants, light_positions) == 320 + 52 * particle_layout::maximum_effects &&
    offsetof(FrameConstants, light_colors) == 320 + 52 * particle_layout::maximum_effects + 16 * particle_layout::maximum_lights &&
    offsetof(FrameConstants, light_state) == 320 + 52 * particle_layout::maximum_effects + 32 * particle_layout::maximum_lights &&
    offsetof(FrameConstants, clock) == 336 + 52 * particle_layout::maximum_effects + 32 * particle_layout::maximum_lights &&
    offsetof(FrameConstants, up_billboard) == 384 + 52 * particle_layout::maximum_effects + 32 * particle_layout::maximum_lights &&
    sizeof(FrameConstants) == 400 + 52 * particle_layout::maximum_effects + 32 * particle_layout::maximum_lights);
}
