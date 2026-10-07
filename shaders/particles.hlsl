#include "effect_classes.hlsli"
#include "explosion.hlsli"
#include "frame_constants.hlsli"
#include "lights.hlsli"

struct DrawConstants
{
    uint record_count;
    uint list_first;
    uint list_count;
    uint depth_scale;
};
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<DrawConstants> draw : register(b0);
ByteAddressBuffer particles : register(t0);
ByteAddressBuffer alive : register(t1);
ByteAddressBuffer classes : register(t2);
Texture2D<float> scene_depth : register(t9);

struct PixelInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float occlusion : TEXCOORD1;
    float distance : TEXCOORD2;
    float softness : TEXCOORD3;
    float shaded : TEXCOORD4;
    float4 lighting : TEXCOORD5;
};

float cloud_transmittance(Layer layer, Flow flow, float3 origin, float3 position, float seconds)
{
    if (flow.cloud_radius <= 0 || layer.shadow_density <= 0) return 1;
    float2 ring = ring_at(flow, seconds);
    float3 centre = origin + float3(0, ring.x, 0);
    float radius = (ring.y + flow.core_radius) * flow.cloud_radius;
    float3 offset = position - centre;
    float along = dot(offset, frame.sun_direction.xyz);
    float discriminant = along * along - (dot(offset, offset) - radius * radius);
    if (discriminant <= 0) return 1;
    float root = sqrt(discriminant);
    float chord = max(-along + root, 0) - max(-along - root, 0);
    return exp(-layer.shadow_density * chord);
}

PixelInput vs_main(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    const float2 corners[6] = {
        float2(-1, 1), float2(1, 1), float2(1, -1),
        float2(-1, 1), float2(1, -1), float2(-1, -1)
    };
    uint record = instance < draw.list_count ? alive.Load((draw.list_first + instance) * 4) : draw.record_count;
    PixelInput output = (PixelInput)0;
    if (record >= draw.record_count)
    {
        output.position = float4(0, 0, 0, 1);
        return output;
    }
    uint slot = record / explosion_particles;
    uint effect_class = frame.effect_states[slot].w;
    uint layer_index = group_layer(classes, effect_class, (record % explosion_particles) / simulation_group_size);
    Layer layer = load_layer(classes, effect_class, layer_index);
    bool smoke = smoke_like(layer_index);
    uint address = record * particle_stride;
    float4 position = asfloat(particles.Load4(address + particle_position_offset));
    float lifetime = asfloat(particles.Load(address + particle_velocity_offset + 12));
    float size = lifetime > 0 ? asfloat(particles.Load(address + particle_size_offset)) : 0;
    float age = position.w;
    float2 corner = corners[vertex];
    float progress = lifetime > 0 ? saturate(age / max(lifetime, 0.001)) : 1;
    float temperature = smoke ? layer_temperature(layer, age) : 0;
    if (smoke) size *= 1 + layer.growth * age + layer.expansion * (1 - temperature / max(layer.heat, 1e-3));
    else size *= 1 + layer.progress_growth * progress;
    float softness = max(size, 0.1);
    float distance = max(mul(frame.view_projection, float4(position.xyz, 1)).w, frame.camera_position.w);
    float pixels = size * frame.projection.x / distance;
    float coverage = 1;
    bool flat = pixels > 0 && pixels < 0.5;
    if (flat)
    {
        coverage = 1.5707963 * pixels * pixels;
        size *= 0.5 / pixels;
    }
    float3 world = position.xyz + (corner.x * frame.camera_right.xyz + corner.y * frame.camera_up.xyz) * size;
    if (layer.stretch > 0 && !flat)
    {
        float3 velocity = asfloat(particles.Load3(address + particle_velocity_offset));
        float3 view = normalize(position.xyz - frame.camera_position.xyz);
        float3 trail = velocity * layer.stretch;
        float3 across = trail - view * dot(trail, view);
        float trail_length = length(across);
        if (trail_length > size)
        {
            float3 along = across / trail_length;
            float3 side = normalize(cross(view, along));
            float3 middle = position.xyz - across * 0.5;
            world = middle + along * (corner.x * (0.5 * trail_length + size)) + side * (corner.y * size);
        }
    }
    output.position = mul(frame.view_projection, float4(world, 1));
    output.distance = output.position.w;
    output.softness = softness;
    output.uv = flat ? 0 : corner;
    output.color = float4(lerp(layer.first_color, layer.second_color, progress),
        layer.strength * (1 - progress) * (1 - progress) * coverage);
    output.shaded = 0;
    output.lighting = float4(0, 0, 0, 1);
    if (smoke)
    {
        float opacity = layer.strength * saturate(age * layer.fade_in_rate) * (1 - progress) * (1 - progress) * coverage;
        uint mask = frame.light_state.y;
        float heat = temperature / max(layer.heat, 1e-3);
        float3 albedo = lerp(layer.first_color, layer.second_color, heat);
        float3 added = albedo * point_lights(position.xyz, 0, false);
        if (mask_enabled(mask, emission_mask_bit)) added += float3(1.0, 0.42, 0.12) * (layer.emission * temperature * temperature * temperature);
        if (mask_enabled(mask, shading_mask_bit))
        {
            float shadow = 1;
            if (mask_enabled(mask, shadow_mask_bit))
            {
                uint4 state = frame.effect_states[slot];
                float seconds = float(frame.clock.x - state.z) / 60.0;
                shadow = cloud_transmittance(layer, load_flow(classes, effect_class), frame.effect_origins[slot].xyz, position.xyz, seconds);
            }
            output.shaded = 1;
            output.lighting = float4(added, shadow);
            output.color = float4(albedo, opacity);
        }
        else
        {
            output.color = float4(albedo * 0.3 + added, opacity);
        }
    }
    output.occlusion = smoke ? 1 : 0;
    return output;
}

float4 ps_main(PixelInput input) : SV_Target
{
    float radius = dot(input.uv, input.uv);
    if (radius >= 1) discard;
    uint2 depth_size;
    scene_depth.GetDimensions(depth_size.x, depth_size.y);
    uint2 texel = min(uint2(input.position.xy * draw.depth_scale), depth_size - 1);
    float scene = frame.camera_position.w / max(scene_depth.Load(int3(texel, 0)), 1e-9);
    float fade = saturate((scene - input.distance) / input.softness);
    float alpha = (1.0 - radius) * input.color.a * fade;
    float3 color = input.color.rgb;
    if (input.shaded > 0.5)
    {
        float3 normal = float3(input.uv, sqrt(saturate(1 - radius)));
        float sun = saturate((dot(normal, frame.sun_billboard.xyz) + 0.6) / 1.6) * input.lighting.w;
        float3 sky = lerp(frame.ground_color.rgb, frame.sky_color.rgb, dot(normal, frame.up_billboard.xyz) * 0.5 + 0.5);
        float3 ambient = lerp(sky, dot(sky, 1.0 / 3).xxx, 0.5) + frame.sun_color.rgb * 0.1;
        color = color * (frame.sun_color.rgb * sun + ambient) + input.lighting.rgb;
    }
    return float4(color * alpha, alpha * input.occlusion);
}
