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
    float4 effect_origins[maximum_explosions];
    uint4 effect_order[maximum_explosions / 4];
};
ConstantBuffer<FrameConstants> frame : register(b1);
