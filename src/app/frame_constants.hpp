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
    std::array<float4, particle_layout::maximum_explosions> effect_origins;
    std::array<std::array<std::uint32_t, 4>, particle_layout::maximum_explosions / 4> effect_order;
};
static_assert(particle_layout::maximum_explosions % 4 == 0);
static_assert(sizeof(float4x4) == 64 && sizeof(float4) == 16);
static_assert(offsetof(FrameConstants, inverse_view_projection) == 64 && offsetof(FrameConstants, camera_position) == 128 &&
    offsetof(FrameConstants, camera_right) == 144 && offsetof(FrameConstants, camera_up) == 160 &&
    offsetof(FrameConstants, sun_direction) == 176 && offsetof(FrameConstants, sun_color) == 192 &&
    offsetof(FrameConstants, sky_color) == 208 && offsetof(FrameConstants, horizon_color) == 224 &&
    offsetof(FrameConstants, ground_color) == 240 && offsetof(FrameConstants, fog) == 256 &&
    offsetof(FrameConstants, viewport) == 272 && offsetof(FrameConstants, effect_origins) == 288 &&
    offsetof(FrameConstants, effect_order) == 288 + 16 * particle_layout::maximum_explosions &&
    sizeof(FrameConstants) == 288 + 20 * particle_layout::maximum_explosions);
}
