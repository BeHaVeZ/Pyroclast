#pragma once

#include "particle_footprint.hlsli"
#include "realism.hlsli"

struct RealismFootprint
{
    bool realism;
    bool residual;
    Footprint footprint;
    uint effect_class;
    uint layer_index;
    uint slot;
    bool survivor;
    uint survivor_index;
    float crossfade;
    float heat;
    float temperature;
    float shadow;
    float3 albedo;
    float softness;
};

RealismFootprint realism_footprint(ByteAddressBuffer particles, ByteAddressBuffer classes, uint record)
{
    RealismFootprint result = (RealismFootprint)0;
    uint address = record * particle_stride;
    result.residual = record >= residual_first;
    EffectResidual link = (EffectResidual)0;
    uint packed = 0;
    Rise stored = (Rise)0;
    if (result.residual)
    {
        uint3 words = particles.Load3(address + residual_packed_offset);
        packed = words.x;
        stored = unpack_rise(words.z);
        result.effect_class = packed_class(packed);
        result.layer_index = packed_layer(packed);
        result.slot = maximum_effects;
    }
    else
    {
        result.slot = record / explosion_particles;
        result.effect_class = frame.effect_states[result.slot].w;
        result.layer_index = group_layer(classes, result.effect_class, (record % explosion_particles) / simulation_group_size);
        link = load_effect_residual(result.slot);
    }
    Layer layer = load_layer(classes, result.effect_class, result.layer_index);
    RealismLayer lingering = load_realism_layer(classes, result.effect_class, result.layer_index);
    result.realism = result.residual || lingering_record(link, lingering, result.layer_index);
    if (!result.realism) return result;
    float4 position = asfloat(particles.Load4(address + particle_position_offset));
    float4 velocity = asfloat(particles.Load4(address + particle_velocity_offset));
    float spawn_size = asfloat(particles.Load(address + particle_size_offset));
    float age = max(position.w, 0);
    uint clock = frame.clock.x;
    uint mask = frame.light_state.y;
    bool varied = result.residual ? stored.varied : link.varied;
    uint key = result.residual ? stored.key : (link.varied ? variation_key(frame.effect_states[result.slot].x) : 0);
    result.temperature = layer_temperature(layer, age);
    result.heat = result.temperature / max(layer.heat, 1e-3);
    float reference = spawn_size * (1 + layer.expansion * (1 - result.heat));
    float diffusion = lingering.diffusion_seconds * variation_factor(varied, key, vary_diffusion, 0.7, 1.4);
    float size = min(reference * sqrt(1 + age / max(diffusion, 1e-3)), lingering.size_cap);
    float depth = lingering.optical_depth * saturate(age * layer.fade_in_rate) * pow(saturate(reference / max(size, 1e-4)), lingering.dilution);
    float stride = float(max(lingering.survivor_stride, 1));
    bool survivor = true;
    if (result.residual)
    {
        Cluster cluster = load_cluster(packed_cluster(packed));
        result.survivor_index = record - residual_first - cluster.ring_group * simulation_group_size - lingering.residual_offset;
        result.crossfade = 1;
        result.shadow = packed_shadow(packed);
        if (mask_enabled(mask, rise_mask_bit))
        {
            float column = exp(-0.015 * max(stored.height - position.y, 0));
            result.shadow = lerp(result.shadow, column, saturate((age - velocity.w) / 10));
        }
        uint death = particles.Load(address + residual_death_offset);
        float left = float(death > clock ? death - clock : 0);
        float life = left + max(age - velocity.w, 0) * 60;
        depth *= smoothstep(0, 1, left / max(lingering.end_share * life, 1));
        if (cluster.fade_end != no_fade)
        {
            depth *= smoothstep(0, 1, float(cluster.fade_end > clock ? cluster.fade_end - clock : 0) / float(cluster.fade_ticks));
        }
    }
    else
    {
        uint index = record % explosion_particles - layer.first_group * simulation_group_size;
        survivor = index % lingering.survivor_stride == 0;
        result.survivor_index = index / lingering.survivor_stride;
        result.crossfade = 1 - saturate(float(link.graduation_tick > clock ? link.graduation_tick - clock : 0) / crossfade_ticks);
        result.shadow = 1;
    }
    result.survivor = survivor;
    result.albedo = lerp(layer.first_color, layer.second_color, result.heat);
    if (varied)
    {
        float graduation_seconds = float(load_graduation_ticks(classes, result.effect_class)) / 60;
        float turned = result.residual ? 1 : max(saturate(age / max(graduation_seconds, 1e-3)), result.crossfade);
        result.albedo = lerp(result.albedo, lingering.albedo.rgb * variation_factor(varied, key, vary_darkness, 0.85, 1.1), turned);
    }
    float growth = 1 + (lingering.survivor_area - 1) * result.crossfade;
    if (survivor)
    {
        size *= sqrt(growth);
        depth *= (1 + (stride - 1) * result.crossfade) / growth;
    }
    else depth *= 1 - result.crossfade;
    result.softness = max(size, 0.1);
    Footprint footprint;
    footprint.position = position.xyz;
    footprint.distance = max(mul(frame.view_projection, float4(position.xyz, 1)).w, frame.camera_position.w);
    footprint.pixels = size * frame.projection.x / footprint.distance;
    footprint.flat = footprint.pixels > 0 && footprint.pixels < 0.5;
    if (footprint.flat) size *= 0.5 / footprint.pixels;
    footprint.size = size;
    footprint.trail_pixels = 0;
    footprint.bound = 1.4142136 * size + footprint.distance / max(frame.projection.x, 1e-6);
    float coverage = footprint.flat ? 1.5707963 * footprint.pixels * footprint.pixels : 1;
    footprint.opacity = (1 - exp(-depth)) * coverage;
    result.footprint = footprint;
    return result;
}
