#pragma once

#include "effect_classes.hlsli"
#include "frame_constants.hlsli"

struct Footprint
{
    float3 position;
    float size;
    float pixels;
    bool flat;
    float distance;
    float bound;
    float trail_pixels;
    float opacity;
};

Footprint particle_footprint(ByteAddressBuffer particles, uint record, Layer layer, bool smoke)
{
    Footprint result;
    uint address = record * particle_stride;
    float4 position = asfloat(particles.Load4(address + particle_position_offset));
    float4 velocity = asfloat(particles.Load4(address + particle_velocity_offset));
    float lifetime = velocity.w;
    float size = lifetime > 0 ? asfloat(particles.Load(address + particle_size_offset)) : 0;
    float age = position.w;
    float progress = lifetime > 0 ? saturate(age / max(lifetime, 0.001)) : 1;
    float temperature = smoke ? layer_temperature(layer, age) : 0;
    if (smoke) size *= 1 + layer.growth * age + layer.expansion * (1 - temperature / max(layer.heat, 1e-3));
    else size *= 1 + layer.progress_growth * progress;
    result.position = position.xyz;
    result.distance = max(mul(frame.view_projection, float4(position.xyz, 1)).w, frame.camera_position.w);
    result.pixels = size * frame.projection.x / result.distance;
    result.flat = result.pixels > 0 && result.pixels < 0.5;
    if (result.flat) size *= 0.5 / result.pixels;
    result.size = size;
    float trail = layer.stretch > 0 && !result.flat ? length(velocity.xyz) * layer.stretch : 0;
    result.trail_pixels = trail * frame.projection.x / result.distance;
    result.bound = 1.4142136 * size + trail + result.distance / max(frame.projection.x, 1e-6);
    float coverage = result.flat ? 1.5707963 * result.pixels * result.pixels : 1;
    result.opacity = layer.strength * saturate(age * layer.fade_in_rate) * (1 - progress) * (1 - progress) * coverage;
    return result;
}

bool sphere_outside_view(float3 centre, float radius)
{
    float4x4 m = frame.view_projection;
    float4 planes[5] = {m[3] + m[0], m[3] - m[0], m[3] + m[1], m[3] - m[1], m[3] - m[2]};
    [unroll] for (uint index = 0; index < 5; ++index)
    {
        float4 plane = planes[index];
        if (dot(plane.xyz, centre) + plane.w < -radius * length(plane.xyz)) return true;
    }
    return false;
}

float screen_area(Footprint footprint)
{
    float4 clip = mul(frame.view_projection, float4(footprint.position, 1));
    float w = max(clip.w, frame.camera_position.w);
    float2 centre = float2(clip.x / w * 0.5 + 0.5, 0.5 - clip.y / w * 0.5) * frame.viewport.xy;
    float radius = footprint.flat ? 0.5 : footprint.pixels;
    float area = footprint.flat ? 1 : 3.1415927 * radius * radius + 2 * radius * footprint.trail_pixels;
    float half_extent = radius + 0.5 * footprint.trail_pixels;
    float2 low = clamp(centre - half_extent, 0, frame.viewport.xy);
    float2 high = clamp(centre + half_extent, 0, frame.viewport.xy);
    float2 inside = max(high - low, 0);
    float share = inside.x * inside.y / max(4 * half_extent * half_extent, 1e-6);
    return area * saturate(share);
}
