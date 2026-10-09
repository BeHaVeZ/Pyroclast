#pragma once

#include "particle_layout.hlsli"
#ifdef PYROCLAST_REALISM
#include "realism_layout.hlsli"
#endif

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
#endif
};
ConstantBuffer<FrameConstants> frame : register(b1);
