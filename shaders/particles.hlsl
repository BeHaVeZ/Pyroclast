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
    uint local = record % explosion_particles;
    bool smoke = local < smoke_particles;
    uint emissive_local = local - smoke_particles;
    uint address = record * particle_stride;
    float4 position = asfloat(particles.Load4(address + particle_position_offset));
    float lifetime = asfloat(particles.Load(address + particle_velocity_offset + 12));
    float size = lifetime > 0 ? asfloat(particles.Load(address + particle_size_offset)) : 0;
    float age = position.w;
    float2 corner = corners[vertex];
    float progress = lifetime > 0 ? saturate(age / max(lifetime, 0.001)) : 1;
    bool fireball = !smoke && emissive_local < fireball_particles;
    if (smoke) size *= 1 + 0.55 * age;
    else if (fireball) size *= 1 + 1.8 * progress;
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
    float3 hot = float3(4.0, 2.4, 0.3);
    float3 cool = float3(1.8, 0.03, 0.003);
    output.color = float4(lerp(hot, cool, progress),
        (fireball ? 0.05 : 0.6) * (1 - progress) * (1 - progress) * coverage);
    if (smoke)
    {
        float temperature = smoke_temperature(age);
        float opacity = 0.08 * saturate(age / 0.25) * (1 - progress) * (1 - progress) * coverage;
        output.color = float4(lerp(float3(0.11, 0.12, 0.13), float3(0.20, 0.10, 0.045), temperature), opacity);
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
