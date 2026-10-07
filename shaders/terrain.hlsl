#include "frame_constants.hlsli"
#include "terrain.hlsli"

ByteAddressBuffer heights : register(t0);

struct PixelInput
{
    float4 position : SV_Position;
    float3 world : TEXCOORD0;
    float3 normal : NORMAL;
};

PixelInput vs_main(uint vertex : SV_VertexID)
{
    const uint2 corners[6] = {uint2(0, 0), uint2(1, 0), uint2(1, 1), uint2(0, 0), uint2(1, 1), uint2(0, 1)};
    const uint cells = terrain_resolution - 1;
    uint cell = min(vertex / 6, cells * cells - 1);
    uint2 sample = uint2(cell % cells, cell / cells) + corners[vertex % 6];
    float height = terrain_sample(heights, sample.x, sample.y);
    float dx = terrain_sample(heights, sample.x + 1, sample.y) - terrain_sample(heights, max(sample.x, 1) - 1, sample.y);
    float dz = terrain_sample(heights, sample.x, sample.y + 1) - terrain_sample(heights, sample.x, max(sample.y, 1) - 1);
    PixelInput output;
    output.world = float3(float2(sample) * terrain_spacing - terrain_half_extent, 0).xzy;
    output.world.y = height;
    output.normal = normalize(float3(-dx, 2 * terrain_spacing, -dz));
    output.position = mul(frame.view_projection, float4(output.world, 1));
    return output;
}

float grid_line(float2 xz, float spacing)
{
    float2 coordinate = xz / spacing;
    float2 distance = abs(frac(coordinate - 0.5) - 0.5) / max(fwidth(coordinate), 1e-4);
    return 1 - saturate(min(distance.x, distance.y));
}

float4 ps_main(PixelInput input) : SV_Target
{
    float3 normal = normalize(input.normal);
    float range = smoothstep(80, 140, length(input.world.xz));
    float3 albedo = lerp(float3(0.42, 0.37, 0.30), float3(0.27, 0.34, 0.19), range);
    albedo *= 1 - 0.25 * grid_line(input.world.xz, 10) - 0.35 * grid_line(input.world.xz, 50);
    float sun = saturate(dot(normal, frame.sun_direction.xyz));
    float3 ambient = lerp(frame.ground_color.rgb, frame.sky_color.rgb, normal.y * 0.5 + 0.5);
    float3 color = albedo * (frame.sun_color.rgb * sun + ambient);
    float distance = length(input.world - frame.camera_position.xyz);
    float visibility = exp(-frame.fog.a * distance);
    return float4(lerp(frame.fog.rgb, color, visibility), 1);
}
