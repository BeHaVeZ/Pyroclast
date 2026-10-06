struct ViewParameters { float aspect_ratio; };
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<ViewParameters> view : register(b0);

struct PixelInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float fade : TEXCOORD1;
};

PixelInput vs_main(float2 position : POSITION, float3 life : TEXCOORD0, uint vertex : SV_VertexID)
{
    const float2 corners[6] = {
        float2(-1, 1), float2(1, 1), float2(1, -1),
        float2(-1, 1), float2(1, -1), float2(-1, -1)
    };
    PixelInput output;
    float2 corner = corners[vertex];
    float size = life.y > 0 ? life.z : 0;
    output.position = float4((position + corner * size) * float2(1.0 / view.aspect_ratio, 1), 0, 1);
    output.uv = corner;
    output.fade = life.y > 0 ? saturate(1.0 - life.x / max(life.y, 0.001)) : 0;
    return output;
}

float4 ps_main(PixelInput input) : SV_Target
{
    float radius = dot(input.uv, input.uv);
    if (radius >= 1) discard;
    return float4(0.25, 0.55, 1.0, (1.0 - radius) * input.fade * 0.3);
}
