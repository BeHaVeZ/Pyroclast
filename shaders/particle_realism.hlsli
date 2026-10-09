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
    float stretch;
    float2 stretch_axis;
    float age;
    bool varied;
    uint key;
    uint id;
    float spawn_size;
    uint pattern;
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
    result.age = age;
    result.varied = varied;
    result.key = key;
    result.id = result.residual ? 0 : frame.effect_states[result.slot].x * explosion_particles + record % explosion_particles;
    result.temperature = layer_temperature(layer, age);
    result.heat = result.temperature / max(layer.heat, 1e-3);
    float reference = spawn_size * (1 + layer.expansion * (1 - result.heat));
    float diffusion = lingering.diffusion_seconds * variation_factor(varied, key, vary_diffusion, 0.7, 1.4);
    float unbounded = reference * sqrt(1 + age / max(diffusion, 1e-3));
    float size = min(unbounded, lingering.size_cap);
    uint slab_index = result.residual ? 0 : record % explosion_particles - layer.first_group * simulation_group_size;
    bool skirt_record = !result.residual && link.skirt && result.layer_index == layer_ground_dust && ((slab_index >> 3) & 1) != 0;
    float fade_in_rate = skirt_record ? max(layer.fade_in_rate, 40) : layer.fade_in_rate;
    float depth = lingering.optical_depth * saturate(age * fade_in_rate) * pow(saturate(reference / max(size, 1e-4)), lingering.dilution);
    float graduation_seconds = float(load_graduation_ticks(classes, result.effect_class)) / 60;
    if (result.layer_index == layer_ground_dust) depth *= lerp(0.3, 1, exp(-max(age - graduation_seconds, 0) / 20));
    else depth *= pow(saturate(size / max(unbounded, 1e-4)), lingering.dilution * load_late_dilution(classes, result.effect_class));
    result.stretch = 1;
    result.stretch_axis = float2(1, 0);
    float wind_length = length(frame.wind.xz);
    if (wind_length > 1e-3 && age > graduation_seconds)
    {
        float3 along = float3(frame.wind.x, 0, frame.wind.z) / wind_length;
        float2 axis = float2(dot(along, frame.camera_right.xyz), dot(along, frame.camera_up.xyz));
        float projected = length(axis);
        if (projected > 1e-3)
        {
            result.stretch = 1 + 0.8 * saturate((age - graduation_seconds) / 40) * projected;
            result.stretch_axis = axis / projected;
        }
    }
    RealismLook look = load_realism_look(classes, result.effect_class);
    bool sooty = look.young_seconds > 0 && result.layer_index != layer_ground_dust;
    if (sooty) depth *= 1 + look.boost * exp(-age / max(look.boost_seconds, 1e-3));
    float stride = float(max(lingering.survivor_stride, 1));
    bool survivor = true;
    if (result.residual)
    {
        Cluster cluster = load_cluster(packed_cluster(packed));
        result.survivor_index = record - residual_first - cluster.ring_group * simulation_group_size - lingering.residual_offset;
        result.crossfade = 1;
        result.shadow = pow(packed_shadow(packed), max(frame.residual.z, 1));
        if (mask_enabled(mask, rise_mask_bit))
        {
            float column = exp(-0.015 * max(stored.height - position.y, 0));
            result.shadow *= lerp(1, column, saturate((age - velocity.w) / 10));
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
    result.spawn_size = spawn_size;
    uint tag = result.residual ? packed_tag(packed) : link.tag;
    result.pattern = realism_hash(tag * 7919u + result.layer_index * 131071u + (survivor ? result.survivor_index : 0x10000u + record % explosion_particles));
    if (skirt_record)
    {
        float young = 1 - max(saturate(age / max(graduation_seconds, 1e-3)), result.crossfade);
        size *= 1 + 0.6 * young;
        depth *= 1 + 1.5 * young;
    }
    result.albedo = lerp(layer.first_color, layer.second_color, result.heat * result.heat);
    if (varied)
    {
        float3 lingering_colour = lingering.albedo.rgb * variation_factor(varied, key, vary_darkness, 0.85, 1.1);
        if (sooty)
        {
            float3 soot = result.layer_index == layer_plume ? look.plume_soot : look.smoke_soot;
            if (!result.residual && jet_record(look, result.layer_index, slab_index))
            {
                soot = 0.45 * load_layer(classes, result.effect_class, layer_ground_dust).first_color;
            }
            result.albedo = lerp(soot, lingering_colour, result.residual ? 1 : max(saturate(age / look.young_seconds), result.crossfade));
        }
        else
        {
            float turned = result.residual ? 1 : max(saturate(age / max(graduation_seconds, 1e-3)), result.crossfade);
            result.albedo = lerp(result.albedo, lingering_colour, turned);
        }
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
    footprint.bound = 1.4142136 * size * sqrt(result.stretch) + footprint.distance / max(frame.projection.x, 1e-6);
    float coverage = footprint.flat ? 1.5707963 * footprint.pixels * footprint.pixels : 1;
    footprint.opacity = (1 - exp(-depth)) * coverage;
    result.footprint = footprint;
    return result;
}
