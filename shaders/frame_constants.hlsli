#pragma once

#include "particle_layout.hlsli"

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
};
ConstantBuffer<FrameConstants> frame : register(b1);
