Texture2D<float4> source : register(t0);
#ifdef __spirv__
[[vk::binding(1, 0)]]
#endif
SamplerState linear_clamp : register(s0);

struct PixelInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

PixelInput vs_main(uint vertex : SV_VertexID)
{
    PixelInput output;
    output.uv = float2((vertex << 1) & 2, vertex & 2);
    output.position = float4(output.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}

float4 ps_main(PixelInput input) : SV_Target
{
    return source.SampleLevel(linear_clamp, input.uv, 0);
}
