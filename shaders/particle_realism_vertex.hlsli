#pragma once

#include "particle_realism.hlsli"

PixelInput realism_vertex(uint vertex, RealismFootprint realism)
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
    output.position = mul(frame.view_projection, float4(world, 1));
    output.distance = output.position.w;
    output.softness = realism.softness;
    output.uv = footprint.flat ? 0 : corner;
    uint mask = frame.light_state.y;
    float3 albedo = lerp(layer.first_color, layer.second_color, realism.heat);
    float3 added = albedo * point_lights(footprint.position, 0, false);
    if (mask_enabled(mask, emission_mask_bit))
    {
        added += float3(1.0, 0.42, 0.12) * (layer.emission * realism.temperature * realism.temperature * realism.temperature);
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
                shadow = cloud_transmittance(layer, load_flow(classes, realism.effect_class), frame.effect_origins[realism.slot].xyz,
                    footprint.position, seconds);
                if (realism.survivor && realism.crossfade >= 1) shadow = float(uint(saturate(shadow) * 255 + 0.5)) / 255;
            }
        }
        output.shaded = footprint.flat ? 2 : 1;
        output.lighting = float4(added, shadow);
        output.color = float4(albedo, footprint.opacity);
    }
    else
    {
        output.color = float4(albedo * 0.3 + added, footprint.opacity);
    }
    output.occlusion = 1;
    return output;
}

uint drawn_record(uint instance)
{
    return instance < draw.list_count ? alive.Load((draw.list_first + instance) * 4) : draw.record_count;
}
