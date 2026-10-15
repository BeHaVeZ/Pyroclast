#pragma once

#include "particle_footprint.hlsli"
#include "realism.hlsli"
#ifdef PYROCLAST_ROTOR
#include "rotor_flow.hlsli"
#include "jet_flow.hlsli"
#endif

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

#ifdef PYROCLAST_ROTOR
float3 rotor_draw_air(float3 position, float size)
{
    RotorFlow flow = rotor_flow_at(frame.rotor_draw[0], frame.rotor_draw[1], frame.rotor_draw[2]);
    if (!(flow.inflow > 0)) return 0;
    float2 axis = flow.hub.xz + flow.skew * rotor_skew_offset(flow.hub.y - position.y);
    float2 offset = position.xz - axis;
    float r = length(offset);
    float3 probe = position;
    if (r > 1e-3) probe.xz -= offset / r * min(rotor_shred_reach * size, r);
    return rotor_air(flow, probe, flow.hub.y - flow.height);
}

void rotor_stretch(inout RealismFootprint result, float3 rotor)
{
    float speed = length(rotor);
    if (speed <= 0) return;
    float3 along = rotor + frame.wind.xyz;
    float reach = length(along);
    if (reach < 1e-3) return;
    along /= reach;
    float2 axis = float2(dot(along, frame.camera_right.xyz), dot(along, frame.camera_up.xyz));
    float projected = length(axis);
    if (projected < 1e-3) return;
    axis /= projected;
    float weight = saturate(speed / rotor_coupling_speed);
    float2 wind_axis = result.stretch_axis;
    float2 doubled = lerp((result.stretch - 1) * float2(wind_axis.x * wind_axis.x - wind_axis.y * wind_axis.y, 2 * wind_axis.x * wind_axis.y),
        3 * saturate(speed / 15) * projected * float2(axis.x * axis.x - axis.y * axis.y, 2 * axis.x * axis.y), weight);
    float excess = length(doubled);
    result.stretch = 1 + excess;
    if (excess > 1e-4)
    {
        float2 unit = doubled / excess;
        result.stretch_axis = float2(sqrt(saturate(0.5 * (1 + unit.x))), (unit.y < 0 ? -1 : 1) * sqrt(saturate(0.5 * (1 - unit.x))));
    }
    result.footprint.bound = 1.4142136 * result.footprint.size * sqrt(result.stretch) +
        result.footprint.distance / max(frame.projection.x, 1e-6);
}
#endif

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
    RealismLook look = load_realism_look(classes, result.effect_class);
    bool trail_record = !result.residual && jet_record(look, result.layer_index, slab_index);
    float fade_in_rate = skirt_record ? max(layer.fade_in_rate, 40) : trail_record ? max(layer.fade_in_rate, 12) : layer.fade_in_rate;
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
        depth *= 1 + 2 * young;
    }
    TrailParameters trail = (TrailParameters)0;
    if (trail_record)
    {
        trail = load_trail(classes, result.effect_class);
        depth *= 1 + trail.knot_density * exp(-age / max(trail.hang_seconds, 1e-3)) * (1 - result.crossfade);
    }
    result.albedo = lerp(layer.first_color, layer.second_color, result.heat * result.heat);
    if (varied)
    {
        float3 lingering_colour = lingering.albedo.rgb * variation_factor(varied, key, vary_darkness, 0.85, 1.1);
        if (sooty)
        {
            float3 soot = result.layer_index == layer_plume ? look.plume_soot : look.smoke_soot;
            if (trail_record) soot = trail.knot_soot * load_layer(classes, result.effect_class, layer_ground_dust).first_color;
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
#ifdef PYROCLAST_ROTOR
    float3 rotor = rotor_draw_air(position.xyz, size);
    float3 jet = jet_draw_air(position.xyz, size);
    float shred = max(smoothstep(rotor_shred_start, rotor_shred_full, length(rotor)), smoothstep(jet_shred_start, jet_shred_full, length(jet)));
    size *= 1 - rotor_shred_size * shred;
    depth *= 1 - rotor_shred_depth * shred;
#endif
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
#ifdef PYROCLAST_ROTOR
    rotor_stretch(result, rotor + jet);
#endif
    return result;
}
#ifdef PYROCLAST_ROTOR
#include "rotor_footprint.hlsli"
#endif
