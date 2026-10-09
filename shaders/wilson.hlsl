#define PYROCLAST_REALISM
#include "frame_constants.hlsli"
#include "blast_list.hlsli"

Texture2D<float> scene_depth : register(t9);

static const float wilson_density = 0.5;

float wilson_hash(float3 cell, uint seed)
{
    uint3 q = uint3(int3(floor(cell)) + 4096);
    uint h = q.x * 0x8da6b343u ^ q.y * 0xd8163841u ^ q.z * 0xcb1ab31fu ^ seed;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return float(h & 0xffffu) / 65535.0;
}

float wilson_noise(float3 p, uint seed)
{
    float3 cell = floor(p);
    float3 f = p - cell;
    float3 s = f * f * (3 - 2 * f);
    float n000 = wilson_hash(cell, seed);
    float n100 = wilson_hash(cell + float3(1, 0, 0), seed);
    float n010 = wilson_hash(cell + float3(0, 1, 0), seed);
    float n110 = wilson_hash(cell + float3(1, 1, 0), seed);
    float n001 = wilson_hash(cell + float3(0, 0, 1), seed);
    float n101 = wilson_hash(cell + float3(1, 0, 1), seed);
    float n011 = wilson_hash(cell + float3(0, 1, 1), seed);
    float n111 = wilson_hash(cell + float3(1, 1, 1), seed);
    return lerp(lerp(lerp(n000, n100, s.x), lerp(n010, n110, s.x), s.y), lerp(lerp(n001, n101, s.x), lerp(n011, n111, s.x), s.y), s.z);
}

float wilson_phase(float g, float cosine)
{
    return (1 - g * g) / pow(max(1 + g * g - 2 * g * cosine, 1e-4), 1.5);
}

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
        asuint(frame.blast_fronts[instance].w) != 0;
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
    uint radii = asuint(frame.blast_fronts[input.blast].w);
    float outer = f16tof32(radii & 0xffff);
    float inner = min(f16tof32(radii >> 16), 0.65 * outer);
    float thickness = max(outer - inner, 1e-3);
    float2 pixel = input.position.xy;
    float2 ndc = float2(pixel.x * frame.viewport.z * 2 - 1, 1 - pixel.y * frame.viewport.w * 2);
    float4 near_point = mul(frame.inverse_view_projection, float4(ndc, 1, 1));
    float3 camera = frame.camera_position.xyz;
    float3 ray = normalize(near_point.xyz / near_point.w - camera);
    float3 forward = cross(frame.camera_up.xyz, frame.camera_right.xyz);
    float depth = scene_depth.Load(int3(pixel, 0));
    float limit = depth > 0 ? frame.camera_position.w / depth / max(dot(ray, forward), 1e-3) : 1e9;
    float2 outside = dome_segment(camera, ray, centre, outer, limit);
    float2 inside = inner > 0 ? dome_segment(camera, ray, centre, inner, limit) : float2(0, 0);
    float path = (outside.y - outside.x) - (inside.y - inside.x);
    if (path <= 0) discard;
    float3 middle = camera + ray * (0.5 * (outside.x + outside.y)) - centre;
    float3 direction = middle * rsqrt(max(dot(middle, middle), 1e-8));
    uint seed = frame.effect_states[blast_slot(input.blast)].x;
    float mottle = 0.6 * wilson_noise(direction * 9, seed) + 0.4 * wilson_noise(direction * 23, seed ^ 0x9e3779b9u);
    float alpha = ring.w * (1 - exp(-wilson_density * lerp(0.35, 1.5, mottle) * path / thickness));
    float3 ambient = 0.5 * (frame.sky_color.rgb + frame.horizon_color.rgb);
    ambient = lerp(ambient, dot(ambient, 1.0 / 3).xxx, 0.5);
    float cosine = dot(ray, frame.sun_direction.xyz);
    float phase = 0.8 * wilson_phase(0.6, cosine) + 0.2 * wilson_phase(-0.3, cosine);
    float3 color = 0.95 * (ambient + 0.3 * frame.sun_color.rgb * phase);
    if (((frame.light_state.y >> haze_mask_bit) & 1) != 0)
    {
        float visibility = exp(-frame.fog.a * outside.x);
        return float4(color * alpha * visibility + frame.fog.rgb * (alpha * (1 - visibility)), alpha);
    }
    return float4(color * alpha, alpha);
}
