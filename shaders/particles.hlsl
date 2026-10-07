#include "explosion.hlsli"
#include "frame_constants.hlsli"

struct DrawConstants { uint first_record; uint layer; uint record_count; };
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<DrawConstants> draw : register(b0);
ByteAddressBuffer particles : register(t0);

struct PixelInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

PixelInput vs_main(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    const float2 corners[6] = {
        float2(-1, 1), float2(1, 1), float2(1, -1),
        float2(-1, 1), float2(1, -1), float2(-1, -1)
    };
    uint record = draw.first_record + instance;
    PixelInput output = (PixelInput)0;
    if (record >= draw.record_count)
    {
        output.position = float4(0, 0, 0, 1);
        return output;
    }
    uint address = record * particle_stride;
    float4 position = asfloat(particles.Load4(address + particle_position_offset));
    float lifetime = asfloat(particles.Load(address + particle_velocity_offset + 12));
    float size = lifetime > 0 ? asfloat(particles.Load(address + particle_size_offset)) : 0;
    float age = position.w;
    float2 corner = corners[vertex];
    float progress = lifetime > 0 ? saturate(age / max(lifetime, 0.001)) : 1;
    bool smoke = draw.layer == particle_layer_smoke;
    bool fireball = !smoke && instance % emissive_particles < fireball_particles;
    if (smoke) size *= 1 + 0.55 * age;
    else if (fireball) size *= 1 + 1.8 * progress;
    float3 world = position.xyz + (corner.x * frame.camera_right.xyz + corner.y * frame.camera_up.xyz) * size;
    output.position = mul(frame.view_projection, float4(world, 1));
    output.uv = corner;
    float3 hot = float3(1.0, 0.8, 0.3);
    float3 cool = float3(0.7, 0.08, 0.01);
    output.color = float4(lerp(hot, cool, progress),
        (fireball ? 0.05 : 0.6) * (1 - progress) * (1 - progress));
    if (smoke)
    {
        float temperature = smoke_temperature(age);
        float opacity = 0.08 * saturate(age / 0.25) * (1 - progress) * (1 - progress);
        output.color = float4(lerp(float3(0.30, 0.32, 0.34), float3(0.42, 0.30, 0.20), temperature), opacity);
    }
    return output;
}

float4 ps_main(PixelInput input) : SV_Target
{
    float radius = dot(input.uv, input.uv);
    if (radius >= 1) discard;
    float alpha = (1.0 - radius) * input.color.a;
    return float4(input.color.rgb * alpha, alpha);
}
