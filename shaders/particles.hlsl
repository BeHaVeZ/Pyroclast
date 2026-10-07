#include "explosion.hlsli"

struct DrawConstants { uint first_record; uint layer; };
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<DrawConstants> draw : register(b0);
struct FrameConstants { float aspect_ratio; };
ConstantBuffer<FrameConstants> frame : register(b1);
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
    uint address = (draw.first_record + instance) * particle_stride;
    float2 position = asfloat(particles.Load2(address + particle_motion_offset));
    float3 life = asfloat(particles.Load3(address + particle_life_offset));
    PixelInput output;
    float2 corner = corners[vertex];
    float size = life.y > 0 ? life.z : 0;
    float progress = life.y > 0 ? saturate(life.x / max(life.y, 0.001)) : 1;
    bool smoke = draw.layer == particle_layer_smoke;
    bool fireball = !smoke && instance % emissive_particles < fireball_particles;
    if (smoke) size *= 1 + 0.55 * life.x;
    else if (fireball) size *= 1 + 1.8 * progress;
    output.position = float4((position + corner * size) * float2(1.0 / frame.aspect_ratio, 1), 0, 1);
    output.uv = corner;
    float3 hot = float3(1.0, 0.8, 0.3);
    float3 cool = float3(0.7, 0.08, 0.01);
    output.color = float4(lerp(hot, cool, progress),
        (fireball ? 0.05 : 0.6) * (1 - progress) * (1 - progress));
    if (smoke)
    {
        float temperature = smoke_temperature(life.x);
        float opacity = 0.08 * saturate(life.x / 0.25) * (1 - progress) * (1 - progress);
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
