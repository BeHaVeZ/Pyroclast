#include "frame_constants.hlsli"
#include "lights.hlsli"
#include "terrain.hlsli"

ByteAddressBuffer vertices : register(t0);
ByteAddressBuffer environment : register(t1);

struct PixelInput
{
    float4 position : SV_Position;
    float3 world : TEXCOORD0;
    float3 normal : NORMAL;
    float shade : TEXCOORD1;
    nointerpolation uint material : TEXCOORD2;
};

PixelInput vs_main(uint vertex : SV_VertexID)
{
    uint address = vertex * prop_vertex_stride;
    uint4 first = vertices.Load4(address);
    float4 second = asfloat(vertices.Load4(address + 16));
    PixelInput output;
    output.world = asfloat(first.xyz);
    output.material = first.w;
    output.normal = second.xyz;
    output.shade = second.w;
    output.position = mul(frame.view_projection, float4(output.world, 1));
    return output;
}

float stripe(float coordinate, float spacing)
{
    float value = coordinate / spacing;
    float width = abs(ddx_fine(value)) + abs(ddy_fine(value));
    return 1 - saturate(abs(frac(value - 0.5) - 0.5) / max(width, 1e-4));
}

float4 ps_main(PixelInput input) : SV_Target
{
    float3 normal = normalize(input.normal);
    float lifts = stripe(input.world.y, 1.2);
    float planks = stripe(input.world.y, 0.3);
    bool vertical = abs(normal.y) < 0.5;
    float3 albedo;
    if (input.material == prop_concrete)
    {
        albedo = float3(0.30, 0.29, 0.27) * (1 - (vertical ? 0.3 * lifts : 0));
    }
    else if (input.material == prop_rock)
    {
        albedo = lerp(float3(0.20, 0.19, 0.17), float3(0.13, 0.115, 0.085), 0.6 * saturate(normal.y));
    }
    else if (input.material == prop_wood)
    {
        albedo = float3(0.34, 0.25, 0.15) * (1 - (vertical ? 0.45 * planks : 0));
    }
    else
    {
        albedo = float3(0.16, 0.18, 0.12);
    }
    albedo *= input.shade;
    float sun = saturate(dot(normal, frame.sun_direction.xyz));
    float3 bounce = frame.ground_color.rgb + frame.sun_color.rgb * saturate(frame.sun_direction.y) * float3(0.13, 0.115, 0.085);
    float3 sky = lerp(frame.horizon_color.rgb, frame.sky_color.rgb, saturate(normal.y));
    float3 ambient = lerp(bounce, sky, normal.y * 0.5 + 0.5);
    float3 color = albedo * (frame.sun_color.rgb * sun + ambient + point_lights(input.world, normal, true));
    float visibility = exp(-frame.fog.a * length(input.world - frame.camera_position.xyz));
    return float4(lerp(frame.fog.rgb, color, visibility), 1);
}
