#pragma once

#include "jet_noise.hlsli"

static const float maximum_haze_pixels = 6;

float2 haze_closest(float3 camera, float3 ray, float3 a, float3 b, float limit, out float3 nearest)
{
    float3 segment = b - a;
    float3 between = camera - a;
    float dd = dot(segment, segment);
    float de = dot(segment, ray);
    float ed = dot(ray, between);
    float sd = dot(segment, between);
    float denominator = dd - de * de;
    float s = denominator > 1e-6 ? saturate((sd - de * ed) / denominator) : 0;
    float t = clamp(dot(a + segment * s - camera, ray), 0, limit);
    s = saturate(dot(camera + ray * t - a, segment) / max(dd, 1e-6));
    nearest = a + segment * s;
    return float2(s, length(camera + ray * t - nearest));
}

float2 haze_term(float2 pixel, float3 camera, float3 ray, float limit, float3 a, float3 b, float age_a, float age_b, float radius_a, float radius_b)
{
    float3 nearest;
    float2 closest = haze_closest(camera, ray, a, b, limit, nearest);
    float age = lerp(age_a, age_b, closest.x);
    float radius = max(lerp(radius_a, radius_b, closest.x), jet_nozzle_radius);
    float distance = closest.y;
    if (distance > 3 * radius) return 0;
    float ratio = jet_nozzle_radius / radius;
    float excess = jet_exhaust_excess * ratio * ratio * exp(-age / jet_haze_seconds) * exp(-distance * distance / (radius * radius));
    float scale = max(1.5 * radius, 1.0);
    float3 drifting = nearest / scale + float3(0, -float(frame.clock.x) * 0.08, 0);
    float2 wander = float2(jet_noise(drifting, 0x2545f491u), jet_noise(drifting, 0x7f4a7c15u)) * 2 - 1;
    return wander * (jet_haze_angle * excess * frame.projection.x);
}

float2 jet_haze_offset(float2 pixel)
{
    uint count = min(frame.jet_state.x, jet_knot_limit);
    uint step = frame.jet_state.z;
    float4 left = frame.jet_heads[step * 4];
    bool head = left.w > 0;
    if (count == 0 && !head) return 0;
    float depth = scene_depth.Load(int3(pixel, 0));
    float2 ndc = float2(pixel.x * frame.viewport.z * 2 - 1, 1 - pixel.y * frame.viewport.w * 2);
    float4 near_point = mul(frame.inverse_view_projection, float4(ndc, 1, 1));
    float3 camera = frame.camera_position.xyz;
    float3 ray = normalize(near_point.xyz / near_point.w - camera);
    float3 forward = cross(frame.camera_up.xyz, frame.camera_right.xyz);
    float limit = depth > 0 ? frame.camera_position.w / depth / max(dot(ray, forward), 1e-3) : 1e9;
    const float hot_seconds = 3 * jet_haze_seconds;
    float2 total = 0;
    if (head && frame.jet_state.y != 0 && count != 0)
    {
        float4 velocity = frame.jet_heads[step * 4 + 3];
        float3 centre = frame.jet_heads[step * 4 + 2].xyz;
        float3 nozzle = centre - normalize(velocity.xyz) * jet_nozzle_aft;
        float3 knot = 0.5 * (frame.jet_knots[0].xyz + frame.jet_knots[1].xyz);
        total += haze_term(pixel, camera, ray, limit, nozzle, knot, 0, frame.jet_knots[3].y, jet_nozzle_radius, frame.jet_knots[2].w);
    }
    for (uint knot = 0; knot + 1 < count; ++knot)
    {
        float age = frame.jet_knots[knot * 4 + 3].y;
        if (age > hot_seconds) break;
        if (frame.jet_knots[knot * 4 + 3].x == 0) continue;
        float3 a = 0.5 * (frame.jet_knots[knot * 4].xyz + frame.jet_knots[knot * 4 + 1].xyz);
        float3 b = 0.5 * (frame.jet_knots[knot * 4 + 4].xyz + frame.jet_knots[knot * 4 + 5].xyz);
        total += haze_term(pixel, camera, ray, limit, a, b, age, frame.jet_knots[knot * 4 + 7].y, frame.jet_knots[knot * 4 + 2].w,
            frame.jet_knots[knot * 4 + 6].w);
    }
    float size = length(total);
    return size > maximum_haze_pixels ? total * (maximum_haze_pixels / size) : total;
}
