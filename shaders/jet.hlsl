#include "frame_constants.hlsli"
#include "lights.hlsli"
#include "terrain_layout.hlsli"

ByteAddressBuffer vertices : register(t0);

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
