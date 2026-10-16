#pragma once

#include "jet_layout.hlsli"
#include "rotor_layout.hlsli"

static const float jet_pi = 3.14159265;

float3 jet_segment(float3 position, float3 a, float3 b, float gamma, float core)
{
    float3 r0 = b - a;
    float3 r1 = position - a;
    float3 r2 = position - b;
    float3 c = cross(r1, r2);
    float l1 = length(r1);
    float l2 = length(r2);
    float denominator = dot(c, c) + core * core * dot(r0, r0);
    if (denominator < 1e-9 || l1 < 1e-6 || l2 < 1e-6) return 0;
    return c * (gamma / (4 * jet_pi) * dot(r0, r1 / l1 - r2 / l2) / denominator);
}

bool jet_near(float3 position, float3 a, float3 b, float3 c, float3 d, float window)
{
    float3 low = min(min(a, b), min(c, d)) - window;
    float3 high = max(max(a, b), max(c, d)) + window;
    return all(position >= low) && all(position <= high);
}

float3 jet_mirrored(float3 a, float ground)
{
    return float3(a.x, 2 * ground - a.y, a.z);
}

float3 jet_legs(float3 position, float3 newer_left, float3 newer_right, float newer_gamma, float newer_core, float3 older_left,
    float3 older_right, float older_gamma, float older_core, float ground, float image, float3 newer_centre, float newer_speed,
    float newer_radius, float3 older_centre, float older_speed, float older_radius)
{
    float3 air = 0;
    float gamma = 0.5 * (newer_gamma + older_gamma);
    float core = 0.5 * (newer_core + older_core);
    if (jet_near(position, newer_left, newer_right, older_left, older_right, jet_window))
    {
        air += jet_segment(position, older_left, newer_left, gamma, core) + jet_segment(position, newer_right, older_right, gamma, core);
        if (image > 0)
        {
            air += jet_segment(position, jet_mirrored(older_left, ground), jet_mirrored(newer_left, ground), -gamma * image, core) +
                jet_segment(position, jet_mirrored(newer_right, ground), jet_mirrored(older_right, ground), -gamma * image, core);
        }
    }
    float3 along = newer_centre - older_centre;
    float reach = length(along);
    if (reach > 1e-6)
    {
        float3 direction = along / reach;
        float t = saturate(dot(position - older_centre, direction) / reach);
        float3 offset = position - (older_centre + along * t);
        float radius = lerp(older_radius, newer_radius, t);
        float speed = lerp(older_speed, newer_speed, t);
        float squared = dot(offset, offset);
        if (radius > 0 && squared < 9 * radius * radius) air -= direction * (speed * exp(-squared / (radius * radius)));
    }
    return air;
}

float3 jet_knot_centre(uint knot)
{
    return 0.5 * (frame.jet_knots[knot * 4].xyz + frame.jet_knots[knot * 4 + 1].xyz);
}

bool jet_in_box(float3 position)
{
    return frame.jet_state.x + uint(frame.jet_heads[frame.jet_state.z * 4].w > 0) != 0 &&
        all(position >= frame.jet_box[0].xyz - jet_window) && all(position <= frame.jet_box[1].xyz + jet_window);
}

// Kept in step with jet_air in src/simulation/jet.cpp.
float3 jet_air(float3 position, uint step)
{
    if (!jet_in_box(position)) return 0;
    float3 air = 0;
    uint count = min(frame.jet_state.x, jet_knot_limit);
    for (uint knot = 0; knot + 1 < count; ++knot)
    {
        float4 newer_left = frame.jet_knots[knot * 4];
        float4 newer_right = frame.jet_knots[knot * 4 + 1];
        float4 newer_ground = frame.jet_knots[knot * 4 + 2];
        if (frame.jet_knots[knot * 4 + 3].x == 0) continue;
        float4 older_left = frame.jet_knots[knot * 4 + 4];
        float4 older_right = frame.jet_knots[knot * 4 + 5];
        float4 older_ground = frame.jet_knots[knot * 4 + 6];
        air += jet_legs(position, newer_left.xyz, newer_right.xyz, newer_left.w, newer_right.w, older_left.xyz, older_right.xyz, older_left.w,
            older_right.w, 0.5 * (newer_ground.x + older_ground.x), 0.5 * (newer_ground.y + older_ground.y), jet_knot_centre(knot), newer_ground.z,
            newer_ground.w, jet_knot_centre(knot + 1), older_ground.z, older_ground.w);
    }
    float4 left = frame.jet_heads[step * 4];
    float4 right = frame.jet_heads[step * 4 + 1];
    float4 centre = frame.jet_heads[step * 4 + 2];
    float4 velocity = frame.jet_heads[step * 4 + 3];
    float gamma = left.w;
    if (gamma > 0)
    {
        float core = right.w;
        float speed = length(velocity.xyz);
        float3 forward = velocity.xyz / max(speed, 1e-3);
        float ground = count != 0 ? frame.jet_knots[2].x : centre.y - 1000;
        float image = 1 - smoothstep(2 * jet_vortex_spacing, 4 * jet_vortex_spacing, centre.y - ground);
        if (jet_near(position, left.xyz, right.xyz, left.xyz, right.xyz, jet_window))
        {
            air += jet_segment(position, left.xyz, right.xyz, gamma, core);
            if (image > 0) air += jet_segment(position, jet_mirrored(left.xyz, ground), jet_mirrored(right.xyz, ground), -gamma * image, core);
        }
        if (frame.jet_state.y != 0 && count != 0)
        {
            float nozzle_speed = velocity.w / (rotor_air_density * jet_pi * jet_nozzle_radius * jet_nozzle_radius);
            air += jet_legs(position, left.xyz, right.xyz, gamma, core, frame.jet_knots[0].xyz, frame.jet_knots[1].xyz, frame.jet_knots[0].w,
                frame.jet_knots[1].w, ground, image, centre.xyz - forward * jet_nozzle_aft, nozzle_speed, jet_nozzle_radius, jet_knot_centre(0),
                frame.jet_knots[2].z, frame.jet_knots[2].w);
        }
        float3 offset = position - centre.xyz;
        float distance = length(offset);
        if (distance < jet_body_window && distance > 1e-6)
        {
            float3 normal = offset / distance;
            distance = max(distance, jet_body_radius);
            float a = jet_body_radius;
            air += (normal * (3 * dot(velocity.xyz, normal)) - velocity.xyz) * (a * a * a / (2 * distance * distance * distance));
        }
    }
    float speed_squared = dot(air, air);
    if (speed_squared > jet_speed_limit * jet_speed_limit) air *= jet_speed_limit * rsqrt(speed_squared);
    return air;
}

float3 jet_draw_air(float3 position, float size)
{
    if (!jet_in_box(position)) return 0;
    uint count = min(frame.jet_state.x, jet_knot_limit);
    float nearest = 1e30;
    float3 towards = position;
    for (uint knot = 0; knot + 1 < count; ++knot)
    {
        if (frame.jet_knots[knot * 4 + 3].x == 0) continue;
        float3 a = jet_knot_centre(knot + 1);
        float3 b = jet_knot_centre(knot);
        float3 along = b - a;
        float t = saturate(dot(position - a, along) / max(dot(along, along), 1e-6));
        float3 closest = a + along * t;
        float3 offset = closest - position;
        float squared = dot(offset, offset);
        if (squared < nearest)
        {
            nearest = squared;
            towards = closest;
        }
    }
    uint step = frame.jet_state.z;
    if (frame.jet_heads[step * 4].w > 0)
    {
        float3 centre = frame.jet_heads[step * 4 + 2].xyz;
        float3 offset = centre - position;
        if (dot(offset, offset) < nearest) towards = centre;
    }
    float3 offset = towards - position;
    float distance = length(offset);
    float3 probe = position;
    if (distance > 1e-3) probe += offset / distance * min(jet_shred_reach * size, distance);
    return jet_air(probe, step);
}
