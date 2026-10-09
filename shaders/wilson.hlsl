#define PYROCLAST_REALISM
#include "frame_constants.hlsli"
#include "blast_list.hlsli"

Texture2D<float> scene_depth : register(t9);

static const float wilson_thickness_share = 0.1;
static const float wilson_density = 0.3;

struct WilsonInput
{
    float4 position : SV_Position;
    nointerpolation uint blast : BLAST;
};

WilsonInput vs_wilson(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    WilsonInput output;
    output.blast = instance;
    bool shown = ((frame.light_state.y >> condensation_mask_bit) & 1) != 0 && instance < blast_count() && frame.blast_rings[instance].w > 0 &&
        frame.blast_fronts[instance].w > 0;
    float2 uv = float2((vertex << 1) & 2, vertex & 2);
    output.position = shown ? float4(uv * float2(2, -2) + float2(-1, 1), 0, 1) : float4(0, 0, 0, 1);
    return output;
}

float2 dome_segment(float3 camera, float3 ray, float3 centre, float radius, float limit)
{
    float3 offset = camera - centre;
    float b = dot(offset, ray);
    float c = dot(offset, offset) - radius * radius;
    float disc = b * b - c;
    if (disc <= 0) return float2(0, 0);
    float root = sqrt(disc);
    float near = max(-b - root, 0);
    float far = min(-b + root, limit);
    if (abs(ray.y) > 1e-5)
    {
        float crossing = (centre.y - camera.y) / ray.y;
        if (ray.y > 0) near = max(near, crossing);
        else far = min(far, crossing);
    }
    else if (camera.y < centre.y)
    {
        return float2(0, 0);
    }
    return float2(near, max(far, near));
}

float4 ps_wilson(WilsonInput input) : SV_Target
{
    float4 ring = frame.blast_rings[input.blast];
    float3 centre = frame.effect_origins[blast_slot(input.blast)].xyz;
    float outer = frame.blast_fronts[input.blast].w;
    float thickness = wilson_thickness_share * outer;
    float2 pixel = input.position.xy;
    float2 ndc = float2(pixel.x * frame.viewport.z * 2 - 1, 1 - pixel.y * frame.viewport.w * 2);
    float4 near_point = mul(frame.inverse_view_projection, float4(ndc, 1, 1));
    float3 camera = frame.camera_position.xyz;
    float3 ray = normalize(near_point.xyz / near_point.w - camera);
    float3 forward = cross(frame.camera_up.xyz, frame.camera_right.xyz);
    float depth = scene_depth.Load(int3(pixel, 0));
    float limit = depth > 0 ? frame.camera_position.w / depth / max(dot(ray, forward), 1e-3) : 1e9;
    float2 outside = dome_segment(camera, ray, centre, outer, limit);
    float2 inside = dome_segment(camera, ray, centre, outer - thickness, limit);
    float path = (outside.y - outside.x) - (inside.y - inside.x);
    if (path <= 0) discard;
    float alpha = ring.w * (1 - exp(-wilson_density * path / max(thickness, 1e-3)));
    float3 normal = normalize(camera + ray * outside.x - centre);
    float sun = saturate(0.5 + 0.5 * dot(normal, frame.sun_direction.xyz));
    float3 color = 0.9 * (frame.sky_color.rgb * 0.8 + frame.sun_color.rgb * 0.35 * sun);
    if (((frame.light_state.y >> haze_mask_bit) & 1) != 0)
    {
        float visibility = exp(-frame.fog.a * outside.x);
        return float4(color * alpha * visibility + frame.fog.rgb * (alpha * (1 - visibility)), alpha);
    }
    return float4(color * alpha, alpha);
}
