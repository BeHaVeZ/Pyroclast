#include "explosion.hlsli"

struct ViewParameters { float aspect_ratio; };
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<ViewParameters> view : register(b0);

struct PixelInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

PixelInput vs_main(float2 position : POSITION, float3 life : TEXCOORD0,
    uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    const float2 corners[6] = {
        float2(-1, 1), float2(1, 1), float2(1, -1),
        float2(-1, 1), float2(1, -1), float2(-1, -1)
    };
    PixelInput output;
    float2 corner = corners[vertex];
    float size = life.y > 0 ? life.z : 0;
    float progress = life.y > 0 ? saturate(life.x / max(life.y, 0.001)) : 1;
    bool fireball = instance < fireball_particles;
    if (fireball) size *= 1 + 1.8 * progress;
    output.position = float4((position + corner * size) * float2(1.0 / view.aspect_ratio, 1), 0, 1);
    output.uv = corner;
    float3 hot = float3(1.0, 0.8, 0.3);
    float3 cool = float3(0.7, 0.08, 0.01);
    output.color = float4(lerp(hot, cool, progress),
        (fireball ? 0.05 : 0.6) * (1 - progress) * (1 - progress));
    return output;
}

float4 ps_main(PixelInput input) : SV_Target
{
    float radius = dot(input.uv, input.uv);
    if (radius >= 1) discard;
    return float4(input.color.rgb, (1.0 - radius) * input.color.a);
}
