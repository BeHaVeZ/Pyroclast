#pragma once

#include "particle_layout.hlsli"
#ifdef PYROCLAST_REALISM
#include "realism_layout.hlsli"
#ifdef PYROCLAST_ROTOR
#include "jet_layout.hlsli"
#endif
#endif

// Kept in step with src/app/frame_constants.hpp, whose static_asserts pin each offset; 16-byte members only.
// Colours are linear HDR; camera_position.w is the near plane, light_state.y the mask, clock.x the tick drawn.
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
    float4 effect_origins[maximum_effects];
    float4 effect_normals[maximum_effects];
    uint4 effect_states[maximum_effects];
    uint4 effect_order[maximum_effects / 4];
    float4 light_positions[maximum_lights];
    float4 light_colors[maximum_lights];
    uint4 light_state;
    uint4 clock;
    float4 wind;
    float4 sun_billboard;
    float4 up_billboard;
#ifdef PYROCLAST_REALISM
    uint4 unit_state;
    float4 residual;
    uint4 unit_order[maximum_units / 4];
    uint4 clusters[maximum_clusters / 2];
    uint4 effect_residual[maximum_effects];
    uint4 blast_state;
    uint4 blast_slots;
    float4 blast_rings[maximum_blasts];
    float4 blast_fronts[maximum_blasts];
    float4 blast_returns[maximum_blasts];
#ifdef PYROCLAST_ROTOR
    uint4 rotor_state;
    float4 rotor_hubs[maximum_simulation_steps];
    float4 rotor_wakes[maximum_simulation_steps];
    float4 rotor_jets[maximum_simulation_steps];
    uint4 rotor_births[maximum_simulation_steps];
    float4 rotor_draw[3];
    float4 rotor_discs[6];
    float4 rotor_dust_shadow[2];
    uint4 jet_state;
    float4 jet_box[2];
    float4 jet_heads[maximum_simulation_steps * 4];
    float4 jet_knots[jet_knot_limit * 4];
    uint4 jet_births[maximum_simulation_steps];
    float4 jet_dust_lines[maximum_simulation_steps];
#endif
#endif
};
ConstantBuffer<FrameConstants> frame : register(b1);
