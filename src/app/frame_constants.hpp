#pragma once

#include "../core/math.hpp"
#include "../graphics/jet_layout.hpp"
#include "../graphics/particle_layout.hpp"
#include "../graphics/realism_layout.hpp"

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
    std::array<std::uint32_t, 4> unit_state;
    float4 residual;
    std::array<std::array<std::uint32_t, 4>, realism_layout::maximum_units / 4> unit_order;
    std::array<std::array<std::uint32_t, 4>, realism_layout::maximum_clusters / 2> clusters;
    std::array<std::array<std::uint32_t, 4>, particle_layout::maximum_effects> effect_residual;
    std::array<std::uint32_t, 4> blast_state;
    std::array<std::uint32_t, 4> blast_slots;
    std::array<float4, realism_layout::maximum_blasts> blast_rings;
    std::array<float4, realism_layout::maximum_blasts> blast_fronts;
    std::array<float4, realism_layout::maximum_blasts> blast_returns;
    std::array<std::uint32_t, 4> rotor_state;
    std::array<float4, particle_layout::maximum_simulation_steps> rotor_hubs;
    std::array<float4, particle_layout::maximum_simulation_steps> rotor_wakes;
    std::array<float4, particle_layout::maximum_simulation_steps> rotor_jets;
    std::array<std::array<std::uint32_t, 4>, particle_layout::maximum_simulation_steps> rotor_births;
    std::array<float4, 3> rotor_draw;
    std::array<float4, 6> rotor_discs;
    std::array<float4, 2> rotor_dust_shadow;
    std::array<std::uint32_t, 4> jet_state;
    std::array<float4, 2> jet_box;
    std::array<float4, particle_layout::maximum_simulation_steps * 4> jet_heads;
    std::array<float4, jet_layout::jet_knot_limit * 4> jet_knots;
};
inline constexpr std::size_t legacy_frame_constant_bytes = 400 + 52 * particle_layout::maximum_effects + 32 * particle_layout::maximum_lights;
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
    offsetof(FrameConstants, unit_state) == legacy_frame_constant_bytes && legacy_frame_constant_bytes == 7568);
static_assert(offsetof(FrameConstants, residual) == 7584 && offsetof(FrameConstants, unit_order) == 7600 &&
    offsetof(FrameConstants, clusters) == 9648 && offsetof(FrameConstants, effect_residual) == 12720 &&
    offsetof(FrameConstants, blast_state) == 14768 && offsetof(FrameConstants, blast_slots) == 14784 &&
    offsetof(FrameConstants, blast_rings) == 14800 && offsetof(FrameConstants, blast_fronts) == 15056 && offsetof(FrameConstants, blast_returns) == 15312 &&
    offsetof(FrameConstants, rotor_state) == 15568 && offsetof(FrameConstants, rotor_hubs) == 15584 && offsetof(FrameConstants, rotor_wakes) == 15648 &&
    offsetof(FrameConstants, rotor_jets) == 15712 && offsetof(FrameConstants, rotor_births) == 15776 && offsetof(FrameConstants, rotor_draw) == 15840 &&
    offsetof(FrameConstants, rotor_discs) == 15888 && offsetof(FrameConstants, rotor_dust_shadow) == 15984 && offsetof(FrameConstants, jet_state) == 16016 &&
    offsetof(FrameConstants, jet_box) == 16032 && offsetof(FrameConstants, jet_heads) == 16064 && offsetof(FrameConstants, jet_knots) == 16320 &&
    sizeof(FrameConstants) == 16320 + 64 * jet_layout::jet_knot_limit &&
    particle_layout::maximum_simulation_steps == 4 &&
    realism_layout::maximum_units % 4 == 0 && realism_layout::maximum_clusters % 2 == 0 && sizeof(FrameConstants) <= 32768);
}
