#include "effect_classes.hlsli"
#include "explosion.hlsli"
#include "frame_constants.hlsli"

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
};

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
    if (smoke) size *= 1 + layer.growth * age;
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
    output.position = mul(frame.view_projection, float4(world, 1));
    output.distance = output.position.w;
    output.softness = softness;
    output.uv = flat ? 0 : corner;
    output.color = float4(lerp(layer.first_color, layer.second_color, progress),
        layer.strength * (1 - progress) * (1 - progress) * coverage);
    if (smoke)
    {
        float temperature = smoke_temperature(age);
        float opacity = layer.strength * saturate(age * layer.fade_in_rate) * (1 - progress) * (1 - progress) * coverage;
        output.color = float4(lerp(layer.first_color, layer.second_color, temperature), opacity);
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
    return float4(input.color.rgb * alpha, alpha * input.occlusion);
}
