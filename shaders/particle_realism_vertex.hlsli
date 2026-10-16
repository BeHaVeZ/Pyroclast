#pragma once

#include "particle_realism.hlsli"

struct RealismPixelInput
{
    PixelInput base;
    float4 detail : TEXCOORD6;
};

RealismPixelInput plain_pixel_input(PixelInput base)
{
    RealismPixelInput output;
    output.base = base;
    output.detail = 0;
    return output;
}

RealismPixelInput realism_vertex(uint vertex, RealismFootprint realism)
{
    const float2 corners[6] = {
        float2(-1, 1), float2(1, 1), float2(1, -1),
        float2(-1, 1), float2(1, -1), float2(-1, -1)
    };
    PixelInput output = (PixelInput)0;
    Footprint footprint = realism.footprint;
    Layer layer = load_layer(classes, realism.effect_class, realism.layer_index);
    float2 corner = corners[vertex];
    float3 world = footprint.position + (corner.x * frame.camera_right.xyz + corner.y * frame.camera_up.xyz) * footprint.size;
    if (realism.stretch > 1 && !footprint.flat)
    {
        float root = sqrt(realism.stretch);
        float2 axis = realism.stretch_axis;
        float3 along = axis.x * frame.camera_right.xyz + axis.y * frame.camera_up.xyz;
        float3 across = -axis.y * frame.camera_right.xyz + axis.x * frame.camera_up.xyz;
        float2 rotated = float2(dot(corner, axis), dot(corner, float2(-axis.y, axis.x)));
        world = footprint.position + (rotated.x * root * along + rotated.y / root * across) * footprint.size;
    }
    output.position = mul(frame.view_projection, float4(world, 1));
    output.distance = output.position.w;
    output.softness = realism.softness;
    output.uv = footprint.flat ? 0 : corner;
    uint mask = frame.light_state.y;
    float3 albedo = realism.albedo;
    float3 added = albedo * realism_point_lights(footprint.position);
    if (mask_enabled(mask, emission_mask_bit) && !realism.residual)
    {
        float seconds = float(frame.clock.x - frame.effect_states[realism.slot].z) / 60.0;
        float glow_seconds = load_realism_layer(classes, realism.effect_class, realism.layer_index).albedo.w;
        float envelope = saturate(1 - seconds / max(glow_seconds, 1e-3));
        added += float3(1.0, 0.42, 0.12) * (layer.emission * realism.temperature * realism.temperature * realism.temperature * envelope * envelope);
    }
    output.shaded = 0;
    output.lighting = float4(0, 0, 0, 1);
    if (mask_enabled(mask, shading_mask_bit))
    {
        float shadow = 1;
        if (mask_enabled(mask, shadow_mask_bit))
        {
            if (realism.residual) shadow = realism.shadow;
            else
            {
                uint4 state = frame.effect_states[realism.slot];
                float seconds = float(frame.clock.x - state.z) / 60.0;
                EffectResidual link = load_effect_residual(realism.slot);
                float3 centre = frame.effect_origins[realism.slot].xyz;
                if (mask_enabled(mask, shear_mask_bit)) centre.xz += link.drift;
                float young = cloud_transmittance(layer, load_flow(classes, realism.effect_class), centre, footprint.position, seconds);
                float4 rise = load_rise(classes, realism.effect_class);
                float neutral = frame.effect_origins[realism.slot].y + neutral_height(rise, realism.varied, realism.key, realism.id, realism.layer_index);
                float macro = column_shadow(classes, realism.effect_class, frame.effect_origins[realism.slot].xyz, link.drift, footprint.position,
                    neutral, realism.varied, realism.key);
                if (realism.survivor && realism.crossfade >= 1) macro = float(uint(saturate(macro) * 255 + 0.5)) / 255;
                float graduation_seconds = float(load_graduation_ticks(classes, realism.effect_class)) / 60;
                float turned = max(saturate(realism.age / max(graduation_seconds, 1e-3)), realism.crossfade);
                shadow = lerp(young, pow(macro, max(frame.residual.z, 1)), turned);
            }
        }
        float graduation_seconds = float(load_graduation_ticks(classes, realism.effect_class)) / 60;
        float thin = 0.7 * (realism.residual ? 1 : max(saturate(realism.age / max(graduation_seconds, 1e-3)), realism.crossfade));
        float cosine = dot(normalize(footprint.position - frame.camera_position.xyz), frame.sun_direction.xyz);
        float phase = 0.7 * henyey_greenstein(0.5, cosine) + 0.3 * henyey_greenstein(-0.25, cosine);
        added += albedo * frame.sun_color.rgb * (shadow * phase * thin);
        output.shaded = footprint.flat ? 2 : 1;
        output.lighting = float4(added, shadow * (1 - thin));
        output.color = float4(albedo, footprint.opacity);
    }
    else
    {
        output.color = float4(albedo * 0.3 + added, footprint.opacity);
    }
    output.occlusion = 1;
    RealismPixelInput result = plain_pixel_input(output);
    if (!footprint.flat)
    {
        RealismLayer lingering = load_realism_layer(classes, realism.effect_class, realism.layer_index);
        float lobes = footprint.size / max(realism.spawn_size * sqrt(lingering.survivor_area), 0.05);
        float2 offset = float2(realism.pattern & 4095u, (realism.pattern >> 12) & 4095u) * (64.0 / 4095.0);
        float graduation_seconds = float(load_graduation_ticks(classes, realism.effect_class)) / 60;
        float amplitude = lerp(0.55, 0.8, saturate(realism.age / max(graduation_seconds, 1e-3))) * saturate((footprint.pixels - 12) / 12);
        float2 drift = float2(0, -0.08 * realism.age / max(realism.spawn_size * sqrt(lingering.survivor_area), 0.05));
        result.detail = float4(offset + drift, lobes, amplitude);
    }
    return result;
}

// Flash timing kept in step with realism_light_curve in explosion_presets.cpp.
PixelInput realism_light(PixelInput output, uint record)
{
    if (record >= residual_first || record >= draw.record_count) return output;
    uint slot = record / explosion_particles;
    uint effect_class = frame.effect_states[slot].w;
    uint layer_index = group_layer(classes, effect_class, (record % explosion_particles) / simulation_group_size);
    if (layer_index != layer_flash && layer_index != layer_fireball && layer_index != layer_sparks) return output;
    RealismLook look = load_realism_look(classes, effect_class);
    if (look.young_seconds <= 0 || look.charge_scale <= 0) return output;
    uint address = record * particle_stride;
    float age = asfloat(particles.Load(address + particle_position_offset + 12));
    float lifetime = asfloat(particles.Load(address + particle_velocity_offset + 12));
    float progress = lifetime > 0 ? saturate(age / max(lifetime, 0.001)) : 1;
    float legacy_falloff = max((1 - progress) * (1 - progress), 1e-4);
    uint id = frame.effect_states[slot].x * explosion_particles + record % explosion_particles;
    Layer layer = load_layer(classes, effect_class, layer_index);
    bool shines = true;
    if (layer_index == layer_flash)
    {
        float flash_seconds = 0.04 + 0.01 * look.charge_scale;
        shines = age < flash_seconds;
        output.color.rgb = lerp(float3(12, 10, 7), float3(8, 4, 1), saturate(age / flash_seconds));
    }
    else if (layer_index == layer_fireball)
    {
        float tau = 0.1 * look.charge_scale;
        float temperature = (0.7 + 0.6 * particle_random(id, 16)) * exp(-max(age, 0) / max(tau, 1e-3));
        float cool = min(temperature, 1);
        float brightness = max(layer.first_color.r, max(layer.first_color.g, layer.first_color.b));
        output.color.rgb = brightness * float3(1, 0.15 + 0.7 * cool * cool, 0.03 + 0.5 * cool * cool * cool * cool);
        output.color.a *= 0.8 * temperature * temperature * temperature / legacy_falloff;
        shines = output.color.a * brightness > 0.01;
    }
    else
    {
        shines = particle_random(id, 17) < 0.35;
        output.color.a *= exp(-max(age, 0) / 0.12);
        output.color.g = min(output.color.g, 0.5 * output.color.r);
        shines = shines && output.color.a > 1e-3;
    }
    if (!shines) output.position = float4(0, 0, 0, 1);
    return output;
}

uint drawn_record(uint instance)
{
    return instance < draw.list_count ? alive.Load((draw.list_first + instance) * 4) : draw.record_count;
}
