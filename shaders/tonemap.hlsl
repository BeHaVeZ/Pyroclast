#include "frame_constants.hlsli"

Texture2D<float4> scene : register(t8);

float4 vs_main(uint vertex : SV_VertexID) : SV_Position
{
    float2 uv = float2((vertex << 1) & 2, vertex & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

// Stephen Hill's fit of the ACES reference rendering and output transforms (sRGB primaries in and out).
float3 aces_fitted(float3 color)
{
    const float3x3 input_matrix = float3x3(
        0.59719, 0.35458, 0.04823,
        0.07600, 0.90834, 0.01566,
        0.02840, 0.13383, 0.83777);
    const float3x3 output_matrix = float3x3(
        1.60475, -0.53108, -0.07367,
        -0.10208, 1.10813, -0.00605,
        -0.00327, -0.07276, 1.07602);
    color = mul(input_matrix, color);
    float3 a = color * (color + 0.0245786) - 0.000090537;
    float3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    return saturate(mul(output_matrix, a / b));
}

float3 linear_to_srgb(float3 color)
{
    return select(color <= 0.0031308, color * 12.92, 1.055 * pow(max(color, 0.0031308), 1 / 2.4) - 0.055);
}

float dither_noise(uint2 pixel)
{
    uint value = pixel.x * 0x8da6b343u ^ pixel.y * 0xd8163841u;
    value ^= value >> 15;
    value *= 0x2c1b3c6du;
    value ^= value >> 12;
    return float(value & 0xffffu) / 65536.0;
}

float4 ps_main(float4 position : SV_Position) : SV_Target
{
    uint2 pixel = uint2(position.xy);
    float3 color = scene.Load(int3(pixel, 0)).rgb * frame.exposure.x;
    float3 display = linear_to_srgb(aces_fitted(color));
    display += (dither_noise(pixel) - 0.5) / 255.0;
    return float4(display, 1);
}
