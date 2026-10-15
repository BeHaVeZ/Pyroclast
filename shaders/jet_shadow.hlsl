#include "environment.hlsli"
#include "frame_constants.hlsli"

ByteAddressBuffer environment : register(t0);

struct ShadowConstants
{
    float4 rows[3];
    uint4 state;
};
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<ShadowConstants> shadow : register(b0);

static const uint shadow_cells = 22;

struct PixelInput
{
    float4 position : SV_Position;
    float3 world : TEXCOORD0;
};

PixelInput vs_jet_shadow(uint vertex : SV_VertexID)
{
    const uint cells = terrain_resolution - 1;
    uint cell = min(vertex / 6, shadow_cells * shadow_cells - 1);
    uint corner = vertex % 6;
    int2 index = int2(shadow.state.xy) + int2(cell % shadow_cells, cell / shadow_cells);
    PixelInput output;
    if (any(index < 0) || any(index >= int2(cells, cells)))
    {
        output.world = 0;
        output.position = float4(0, 0, 0, 1);
        return output;
    }
    uint2 sample = uint2(index) + uint2((0x16u >> corner) & 1, (0x34u >> corner) & 1);
    output.world = float3(float2(sample) * terrain_spacing - terrain_half_extent, 0).xzy;
    output.world.y = terrain_sample(environment, sample.x, sample.y) + 0.05;
    output.position = mul(frame.view_projection, float4(output.world, 1));
    return output;
}

float3 to_body(float3 world)
{
    float3 offset = world - float3(shadow.rows[0].w, shadow.rows[1].w, shadow.rows[2].w);
    return float3(dot(float3(shadow.rows[0].x, shadow.rows[1].x, shadow.rows[2].x), offset),
        dot(float3(shadow.rows[0].y, shadow.rows[1].y, shadow.rows[2].y), offset),
        dot(float3(shadow.rows[0].z, shadow.rows[1].z, shadow.rows[2].z), offset));
}

float capsule_shade(float3 origin, float3 direction, float3 a, float3 b, float radius)
{
    float3 segment = b - a;
    float3 between = origin - a;
    float dd = dot(segment, segment);
    float de = dot(segment, direction);
    float ed = dot(direction, between);
    float sd = dot(segment, between);
    float denominator = dd - de * de;
    float s = denominator > 1e-6 ? saturate((sd - de * ed) / denominator) : 0;
    float t = max(dot(a + segment * s - origin, direction), 0);
    s = saturate(dot(origin + direction * t - a, segment) / max(dd, 1e-6));
    float distance = length(origin + direction * t - (a + segment * s));
    float penumbra = 0.0093 * t + 0.05;
    return saturate((radius + penumbra - distance) / (2 * penumbra));
}

float planform_shade(float3 origin, float3 direction, float height, float root_x, float root_z, float tip_x, float tip_z, float root_chord,
    float tip_chord)
{
    if (abs(direction.y) < 1e-4) return 0;
    float t = (height - origin.y) / direction.y;
    if (t <= 0) return 0;
    float2 crossing = origin.xz + direction.xz * t;
    float z = abs(crossing.y);
    float span = (z - root_z) / (tip_z - root_z);
    float penumbra = 0.0093 * t + 0.05;
    float leading = lerp(root_x, tip_x, saturate(span));
    float chord = lerp(root_chord, tip_chord, saturate(span));
    float inside = min(min(z - root_z, tip_z - z), min(leading - crossing.x, crossing.x - (leading - chord)));
    return saturate((inside + penumbra) / (2 * penumbra));
}

float4 ps_jet_shadow(PixelInput input) : SV_Target
{
    float fade = float(shadow.state.z) / 65536.0;
    float3 origin = to_body(input.world);
    float3 direction = to_body(input.world + frame.sun_direction.xyz) - origin;
    float shade = capsule_shade(origin, direction, float3(-7.3, 0, 0), float3(5.0, 0.02, 0), 0.8);
    shade = max(shade, capsule_shade(origin, direction, float3(5.0, 0.02, 0), float3(7.4, -0.05, 0), 0.3));
    shade = max(shade, planform_shade(origin, direction, -0.15, 1.2, 0.85, -2.4, 5.0, 4.6, 1.3));
    shade = max(shade, planform_shade(origin, direction, -0.4, -5.0, 0.95, -6.5, 3.1, 2.3, 0.95));
    shade = max(shade, capsule_shade(origin, direction, float3(-4.5, 1.0, 0), float3(-6.6, 3.2, 0), 0.25));
    float visibility = exp(-frame.fog.a * length(input.world - frame.camera_position.xyz));
    float edge = saturate((terrain_half_extent - max(abs(input.world.x), abs(input.world.z))) / 16);
    float alpha = 0.85 * shade * fade * visibility * edge;
    return float4(0, 0, 0, alpha);
}
