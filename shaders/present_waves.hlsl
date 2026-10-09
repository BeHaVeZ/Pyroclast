#define PYROCLAST_REALISM
#include "tonemap.hlsl"
#include "blast_list.hlsli"

Texture2D<float> scene_depth : register(t9);
SamplerState linear_clamp : register(s0);

static const float maximum_wave_pixels = 24;
static const float air_fade_metres = 10;

float2 ring_offset(float2 pixel, float3 at, float3 outward, float x, float width, float amplitude, float view_depth, float cap)
{
    float pixels = amplitude * frame.projection.x / max(view_depth, 0.1) * (-x / width) * exp(-x * x / (2 * width * width));
    float4 clip = mul(frame.view_projection, float4(at + outward * (0.01 * view_depth), 1));
    if (clip.w < 1e-3) return 0;
    float2 direction = (clip.xy / clip.w * float2(0.5, -0.5) + 0.5) * frame.viewport.xy - pixel;
    float length_squared = dot(direction, direction);
    if (length_squared < 1e-12) return 0;
    return direction * rsqrt(length_squared) * clamp(pixels, -cap, cap);
}

float2 wave_offset(float2 pixel)
{
    float depth = scene_depth.Load(int3(pixel, 0));
    float2 ndc = float2(pixel.x * frame.viewport.z * 2 - 1, 1 - pixel.y * frame.viewport.w * 2);
    float4 near_point = mul(frame.inverse_view_projection, float4(ndc, 1, 1));
    float3 camera = frame.camera_position.xyz;
    float3 ray = normalize(near_point.xyz / near_point.w - camera);
    float3 forward = cross(frame.camera_up.xyz, frame.camera_right.xyz);
    float along = max(dot(ray, forward), 1e-3);
    bool surface = depth > 0;
    float surface_depth = surface ? frame.camera_position.w / depth : 1e9;
    float surface_distance = surface_depth / along;
    float3 point_behind = camera + ray * surface_distance;
    float2 total = 0;
    for (uint index = 0; index < blast_count(); ++index)
    {
        float4 ring = frame.blast_rings[index];
        float radius = ring.x;
        float amplitude = ring.y;
        float width = ring.z;
        float cap = maximum_wave_pixels;
        if (amplitude <= 0) continue;
        float3 origin = frame.effect_origins[blast_slot(index)].xyz;
        if (surface)
        {
            float3 offset = point_behind - origin;
            float reach = length(offset);
            float x = reach - radius;
            if (abs(x) < 4 * width && reach > 1e-3)
            {
                total += ring_offset(pixel, point_behind, offset / reach, x, width, amplitude, surface_depth, cap);
            }
        }
        float closest = dot(origin - camera, ray);
        if (closest > 0 && closest < surface_distance)
        {
            float3 nearest = camera + ray * closest;
            float3 offset = nearest - origin;
            float reach = length(offset);
            float x = reach - radius;
            float fade = saturate(closest * along / air_fade_metres) * saturate((nearest.y - origin.y + 0.5) / 0.5);
            if (fade > 0 && abs(x) < 4 * width && reach > 1e-3)
            {
                total += fade * ring_offset(pixel, nearest, offset / reach, x, width, amplitude, closest * along, cap);
            }
        }
    }
    return total;
}

float4 ps_waves(float4 position : SV_Position) : SV_Target
{
    uint2 pixel = uint2(position.xy);
    float2 offset = ((frame.light_state.y >> waves_mask_bit) & 1) != 0 ? wave_offset(position.xy) : 0;
    float2 uv = (position.xy + offset) * frame.viewport.zw;
    float3 color = scene.SampleLevel(linear_clamp, uv, 0).rgb * frame.exposure.x;
    float3 display = linear_to_srgb(aces_fitted(color));
    display += (dither_noise(pixel) - 0.5) / 255.0;
    return float4(display, 1);
}
