#pragma once

#include "frame_constants.hlsli"

float3 point_lights(float3 position, float3 normal, bool facing)
{
    float3 total = 0;
    uint count = min(frame.light_state.x, maximum_lights);
    for (uint index = 0; index < count; ++index)
    {
        float3 offset = frame.light_positions[index].xyz - position;
        float distance_squared = dot(offset, offset);
        float radius = frame.light_positions[index].w;
        float window = saturate(1 - distance_squared / (radius * radius));
        float falloff = window * window / (1 + distance_squared);
        float cosine = facing ? saturate(dot(normal, offset) * rsqrt(max(distance_squared, 1e-6))) : 1;
        total += frame.light_colors[index].rgb * (falloff * cosine);
    }
    return total;
}
