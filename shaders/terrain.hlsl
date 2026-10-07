#include "frame_constants.hlsli"
#include "lights.hlsli"
#include "terrain.hlsli"

ByteAddressBuffer environment : register(t0);

struct PixelInput
{
    float4 position : SV_Position;
    float3 world : TEXCOORD0;
    float3 normal : NORMAL;
};

PixelInput vs_main(uint vertex : SV_VertexID)
{
    const uint cells = terrain_resolution - 1;
    uint cell = min(vertex / 6, cells * cells - 1);
    uint corner = vertex % 6;
    uint2 sample = uint2(cell % cells, cell / cells) + uint2((0x16u >> corner) & 1, (0x34u >> corner) & 1);
    float height = terrain_sample(environment, sample.x, sample.y);
    float dx = terrain_sample(environment, sample.x + 1, sample.y) - terrain_sample(environment, max(sample.x, 1) - 1, sample.y);
    float dz = terrain_sample(environment, sample.x, sample.y + 1) - terrain_sample(environment, sample.x, max(sample.y, 1) - 1);
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
    float2 width = abs(ddx_fine(coordinate)) + abs(ddy_fine(coordinate));
    float2 distance = abs(frac(coordinate - 0.5) - 0.5) / max(width, 1e-4);
    return 1 - saturate(min(distance.x, distance.y));
}

float4 ps_main(PixelInput input) : SV_Target
{
    float3 normal = normalize(input.normal);
    float range = smoothstep(80, 140, length(input.world.xz));
    float3 albedo = lerp(float3(0.13, 0.115, 0.085), float3(0.05, 0.09, 0.028), range);
    float2 pad_centre = float2(concrete_pad_min_x + concrete_pad_max_x, concrete_pad_min_z + concrete_pad_max_z) * 0.5;
    float2 pad_half = float2(concrete_pad_max_x - concrete_pad_min_x, concrete_pad_max_z - concrete_pad_min_z) * 0.5;
    float2 outside = abs(input.world.xz - pad_centre) - pad_half;
    float pad_distance = max(outside.x, outside.y);
    float pixel = max(abs(ddx_fine(pad_distance)) + abs(ddy_fine(pad_distance)), 1e-4);
    float pad = saturate(0.5 - pad_distance / pixel);
    float3 concrete = float3(0.30, 0.29, 0.27) * (1 - 0.35 * grid_line(input.world.xz - pad_centre, 6));
    albedo = lerp(albedo, concrete, pad);
    albedo *= 1 - 0.25 * grid_line(input.world.xz, 10) - 0.35 * grid_line(input.world.xz, 50);
    float sun = saturate(dot(normal, frame.sun_direction.xyz));
    float3 ambient = lerp(frame.ground_color.rgb, frame.sky_color.rgb, normal.y * 0.5 + 0.5);
    float3 color = albedo * (frame.sun_color.rgb * sun + ambient + point_lights(input.world, normal, true));
    float distance = length(input.world - frame.camera_position.xyz);
    float visibility = exp(-frame.fog.a * distance);
    return float4(lerp(frame.fog.rgb, color, visibility), 1);
}
