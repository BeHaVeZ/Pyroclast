#pragma once

#include "realism.hlsli"

struct JetHead
{
    float3 start;
    float3 velocity;
};

uint jet_count(uint seed, TrailParameters trail)
{
    return trail.jets_min + realism_hash(seed ^ 0x6a09e667u) % max(trail.jets_range, 1u);
}

JetHead jet_head(uint seed, uint jet, uint jets, float3 origin, float3 normal, TrailParameters trail)
{
    uint jet_hash = realism_hash(seed ^ (jet * 0x9e3779b9u + 0x3c6ef372u));
    float azimuth = 6.2831853 * (float(jet) + 0.6 * float(jet_hash & 0xffff) / 65535) / float(jets);
    float tilt = trail.tilt_min + trail.tilt_range * float(jet_hash >> 16) / 65535;
    float3 along = float3(sin(tilt) * cos(azimuth), cos(tilt), sin(tilt) * sin(azimuth));
    float3 direction = around_normal(along, normal);
    JetHead head;
    head.start = origin + direction * 0.3;
    head.velocity = direction * (trail.head_speed * (0.85 + 0.3 * float(realism_hash(jet_hash ^ 0x5bd1e995u) & 0xffff) / 65535));
    return head;
}

float3 jet_head_at(JetHead head, uint ticks)
{
    float t = float(ticks) / 60.0;
    return head.start + head.velocity * t - float3(0, 0.5 * 9.81 * t * (t + 1.0 / 60.0), 0);
}

void realism_spawn(uint slot, uint4 state, uint layer_index, uint index, uint id, float3 origin, float3 normal, Layer layer, bool props,
    inout float4 position, inout float4 velocity, inout float size)
{
    if (layer_index != layer_debris) return;
    ClodParameters clods = load_clods(classes, state.w);
    RealismLook look = load_realism_look(classes, state.w);
    TrailParameters trail = load_trail(classes, state.w);
    if (load_effect_residual(slot).linger && look.jet_speed > 0 && trail.jets_min > 0)
    {
        uint jets = jet_count(state.x, trail);
        if (index < jets)
        {
            JetHead head = jet_head(state.x, index, jets, origin, normal, trail);
            position.xyz = head.start;
            if (props) position.xyz = prop_clip_spawn(environment, origin, position.xyz);
            velocity.xyz = head.velocity;
            velocity.w = layer.lifetime_min + clods.lead_life * layer.lifetime_range;
            size = clods.lead_size * (layer.size_min + layer.size_range);
            position.w = 0;
            return;
        }
    }
    float u = particle_random(id, 5);
    float r = particle_random(id, 4);
    float t = pow(u, clods.size_exponent);
    float chunk = saturate((u - (1 - clods.chunk_share)) / max(clods.chunk_share, 1e-4));
    size = (layer.size_min + layer.size_range * t) * (1 + clods.chunk_boost * chunk * chunk);
    velocity.w = (layer.lifetime_min + r * layer.lifetime_range) * (clods.life_floor + (1 - clods.life_floor) * max(t, chunk));
}
