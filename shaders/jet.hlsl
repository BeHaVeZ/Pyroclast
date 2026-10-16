#include "frame_constants.hlsli"
#include "lights.hlsli"
#include "terrain_layout.hlsli"
#include "jet_noise.hlsli"

ByteAddressBuffer vertices : register(t0);

// Twin of JetConstants in jet_renderer.hpp; state: x the dither level 0-16, then as float bits the nozzle glow,
// the afterburner (0-1) and the vapor cone's opacity (0-1).
struct JetConstants
{
    float4 rows[3];
    uint4 state;
};
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<JetConstants> jet : register(b0);

struct JetPixelInput
{
    float4 position : SV_Position;
    float3 world : TEXCOORD0;
    float3 normal : NORMAL;
    float shade : TEXCOORD1;
    nointerpolation uint material : TEXCOORD2;
};

JetPixelInput vs_jet(uint vertex : SV_VertexID)
{
    uint address = vertex * prop_vertex_stride;
    uint4 first = vertices.Load4(address);
    float4 second = asfloat(vertices.Load4(address + 16));
    float4 body = float4(asfloat(first.xyz), 1);
    JetPixelInput output;
    output.world = float3(dot(jet.rows[0], body), dot(jet.rows[1], body), dot(jet.rows[2], body));
    output.material = first.w;
    output.normal = float3(dot(jet.rows[0].xyz, second.xyz), dot(jet.rows[1].xyz, second.xyz), dot(jet.rows[2].xyz, second.xyz));
    output.shade = second.w;
    output.position = mul(frame.view_projection, float4(output.world, 1));
    return output;
}

static const uint jet_paint = 0;
static const uint jet_dark = 1;
static const uint jet_canopy = 2;
static const uint jet_metal = 3;
static const uint jet_glow = 4;

// The Bayer threshold is computed from its bits: WARP's shader compiler crashed on an indexed table.
float4 ps_jet(JetPixelInput input) : SV_Target
{
    float3 normal = normalize(input.normal);
    float3 view = normalize(frame.camera_position.xyz - input.world);
    if (dot(normal, view) < 0) normal = -normal;
    float3 sky = lerp(frame.horizon_color.rgb, frame.sky_color.rgb, saturate(normal.y));
    float3 bounce = frame.ground_color.rgb + frame.sun_color.rgb * saturate(frame.sun_direction.y) * float3(0.13, 0.115, 0.085);
    float3 ambient = lerp(bounce, sky, normal.y * 0.5 + 0.5);
    float sun = saturate(dot(normal, frame.sun_direction.xyz));
    float3 lit = frame.sun_color.rgb * sun + ambient + point_lights(input.world, normal, true);
    float3 halfway = normalize(view + frame.sun_direction.xyz);
    float highlight = pow(saturate(dot(normal, halfway)), 60) * sun;
    float fresnel = pow(1 - saturate(dot(normal, view)), 5);
    float3 reflected = reflect(-view, normal);
    float3 sky_reflection = reflected.y > 0 ? lerp(frame.horizon_color.rgb, frame.sky_color.rgb, saturate(reflected.y)) : frame.ground_color.rgb;
    float3 color;
    if (input.material == jet_glow)
    {
        color = float3(1.0, 0.32, 0.07) * asfloat(jet.state.y);
    }
    else if (input.material == jet_canopy)
    {
        float reflectance = lerp(0.06, 1, fresnel);
        color = float3(0.025, 0.03, 0.035) * lit + sky_reflection * reflectance + frame.sun_color.rgb * (6 * highlight);
    }
    else
    {
        float3 albedo = input.material == jet_dark ? float3(0.06, 0.06, 0.065) :
            input.material == jet_metal ? float3(0.20, 0.19, 0.18) : float3(0.34, 0.36, 0.38);
        albedo *= input.shade;
        float gloss = input.material == jet_metal ? 0.35 : 0.12;
        color = albedo * lit + sky_reflection * (0.04 + 0.2 * fresnel) * gloss * 4 + frame.sun_color.rgb * (gloss * highlight);
    }
    float visibility = exp(-frame.fog.a * length(input.world - frame.camera_position.xyz));
    color = lerp(frame.fog.rgb, color, visibility);
    uint2 cell = (uint2(input.position.xy) >> 1) & 3u;
    uint mixed = cell.x ^ cell.y;
    uint bayer = ((mixed & 1u) << 3) | ((cell.y & 1u) << 2) | (mixed & 2u) | ((cell.y >> 1) & 1u);
    if (bayer >= jet.state.x) discard;
    return float4(color, 1);
}

float3 jet_world(float3 body)
{
    float4 p = float4(body, 1);
    return float3(dot(jet.rows[0], p), dot(jet.rows[1], p), dot(jet.rows[2], p));
}

float3 jet_world_direction(float3 body)
{
    return float3(dot(jet.rows[0].xyz, body), dot(jet.rows[1].xyz, body), dot(jet.rows[2].xyz, body));
}

struct EffectInput
{
    float4 position : SV_Position;
    float3 world : TEXCOORD0;
    float3 normal : NORMAL;
    float2 shape : TEXCOORD1;
};

EffectInput revolution_vertex(uint vertex, uint rings, uint around, float x0, float x1, float r0, float r1, float exponent)
{
    uint quad = vertex / 6;
    uint corner = vertex % 6;
    uint ring = min(quad / around, rings - 2) + ((0x34u >> corner) & 1);
    uint column = (quad % around + ((0x16u >> corner) & 1)) % around;
    float t = float(ring) / float(rings - 1);
    float angle = 6.2831853 * float(column) / float(around);
    float radius = lerp(r0, r1, pow(t, exponent));
    float3 body = float3(lerp(x0, x1, t), radius * sin(angle), radius * cos(angle));
    float slope = (r1 - r0) * exponent * pow(max(t, 1e-3), exponent - 1) / (x1 - x0);
    float3 normal = normalize(float3(-slope, sin(angle), cos(angle)));
    EffectInput output;
    output.world = jet_world(body);
    output.normal = jet_world_direction(normal);
    output.position = mul(frame.view_projection, float4(output.world, 1));
    output.shape = float2(t, angle);
    return output;
}

static const uint flame_rings = 12;
static const uint flame_around = 12;

EffectInput vs_flame(uint vertex : SV_VertexID)
{
    float burner = asfloat(jet.state.z);
    float reach = 2 + 4 * burner;
    EffectInput output = revolution_vertex(vertex, flame_rings, flame_around, -7.33, -7.33 - reach, 0.40, 0.08, 1.0);
    if (burner <= 0 || jet.state.x == 0) output.position = float4(0, 0, 0, 1);
    return output;
}

float4 ps_flame(EffectInput input) : SV_Target
{
    float burner = asfloat(jet.state.z);
    float3 view = normalize(frame.camera_position.xyz - input.world);
    float facing = pow(abs(dot(normalize(input.normal), view)), 1.5);
    float t = input.shape.x;
    float reach = 2 + 4 * burner;
    float metres = t * reach;
    float diamond = 1 + 1.2 * pow(saturate(cos(6.2831853 * metres / 0.9)), 6) * exp(-metres / 3);
    float flicker = 0.85 + 0.3 * jet_noise(float3(metres * 2, float(frame.clock.x) * 0.9, input.shape.y), 0x51ed270bu);
    float3 hot = lerp(float3(6.0, 4.6, 2.6), float3(4.0, 1.4, 0.35), saturate(t * 2.2));
    float3 color = lerp(hot, float3(0.25, 0.35, 1.2), saturate(t * 1.6 - 0.6));
    float glow = burner * facing * diamond * flicker * (1 - smoothstep(0.75, 1, t));
    float visibility = exp(-frame.fog.a * distance(input.world, frame.camera_position.xyz));
    return float4(color * (glow * visibility), 0);
}

static const uint vapor_rings = 12;
static const uint vapor_around = 28;

EffectInput vs_vapor(uint vertex : SV_VertexID)
{
    EffectInput output = revolution_vertex(vertex, vapor_rings, vapor_around, 3.2, -3.6, 1.0, 5.2, 0.7);
    if (asfloat(jet.state.w) <= 0 || jet.state.x == 0) output.position = float4(0, 0, 0, 1);
    return output;
}

float4 ps_vapor(EffectInput input) : SV_Target
{
    float strength = asfloat(jet.state.w) * float(jet.state.x) / 16;
    float3 ray = normalize(input.world - frame.camera_position.xyz);
    float facing = abs(dot(normalize(input.normal), -ray));
    float t = input.shape.x;
    float limb = 0.75 + 0.25 * pow(1 - facing, 2);
    float body = smoothstep(0.1, 0.6, t) * (1 - smoothstep(0.93, 1, t)) * (0.3 + 0.7 * t * t);
    float3 around = float3(cos(input.shape.y), sin(input.shape.y), 0);
    float mottle = 0.55 * jet_noise(float3(around.xy * 3, t * 6 + float(frame.clock.x) * 0.35), 0x3c6ef372u) +
        0.45 * jet_noise(float3(around.xy * 8, t * 14 - float(frame.clock.x) * 0.5), 0x9e3779b9u);
    float alpha = saturate(1.3 * strength * limb * body * lerp(0.3, 1.4, mottle));
    float3 color = jet_cloud_color(ray);
    float visibility = exp(-frame.fog.a * distance(input.world, frame.camera_position.xyz));
    return float4(color * alpha * visibility + frame.fog.rgb * (alpha * (1 - visibility)), alpha);
}
