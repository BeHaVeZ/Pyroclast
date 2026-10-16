#pragma once

#include "rotor_layout.hlsli"

static const float rotor_pi = 3.14159265;

struct RotorFlow
{
    float3 hub;
    float inflow;
    float2 skew;
    float height;
    float ring;
    float jet_flow;
    float jet_radius;
    float2 ground;
};

RotorFlow rotor_flow_at(float4 hub, float4 wake, float4 jet)
{
    RotorFlow flow;
    flow.hub = hub.xyz;
    flow.inflow = hub.w;
    flow.skew = wake.xy;
    flow.height = wake.z;
    flow.ring = wake.w;
    flow.jet_flow = jet.x;
    flow.jet_radius = jet.y;
    flow.ground = jet.zw;
    return flow;
}

float rotor_contracted_radius(float z)
{
    float lambda = clamp(z * rsqrt(z * z + rotor_radius * rotor_radius), rotor_contraction_floor, 1);
    return rotor_radius * rsqrt(1 + lambda);
}

float rotor_wake_radius(float z)
{
    float x = z - rotor_far_wake_start * rotor_radius;
    return rotor_contracted_radius(z) + rotor_far_wake_growth * 0.5 * (x + sqrt(x * x + rotor_radius * rotor_radius));
}

float rotor_skew_offset(float z)
{
    float blend = rotor_skew_blend * rotor_radius;
    return 0.5 * (z + sqrt(z * z + blend * blend));
}

float rotor_softplus(float u)
{
    return u > 0 ? u + log(1 + exp(-u)) : log(1 + exp(u));
}

float rotor_stream(RotorFlow flow, float s, float z, float y_ground)
{
    float depth = z > 0 ? min(z, flow.height) : z;
    float radius = rotor_wake_radius(depth);
    float tube = rotor_pi * rotor_radius * rotor_radius * flow.inflow * radius / rotor_contracted_radius(depth);
    if (z > 0) tube += flow.jet_flow * smoothstep(0, flow.height, z);
    float k = rotor_fermi;
    float x = s / (radius * radius);
    float profile = (x - (rotor_softplus(k * (x - 1)) - rotor_softplus(-k)) / k) / rotor_fermi_norm;
    float jet = flow.jet_radius;
    float thickness = sqrt(0.25 * jet * jet + rotor_wall_growth * rotor_wall_growth * s);
    float reference = jet * sqrt(0.25 + 4 * rotor_wall_growth * rotor_wall_growth);
    float ground = 1 - exp(-y_ground / thickness);
    float over = thickness / reference - 1;
    float grown = 1 + 0.5 * (over + sqrt(over * over + 0.01));
    float entrained = 1 + (grown - 1) * exp(-y_ground / (rotor_entrainment_height * jet));
    float stream = -tube / (2 * rotor_pi) * profile * ground * entrained;
    if (flow.ring > 0)
    {
        float centre = rotor_ring_radius * rotor_radius;
        float core = rotor_ring_core * rotor_radius;
        float dr = sqrt(s) - centre;
        float dy = y_ground - rotor_ring_height * flow.height;
        stream -= flow.ring * (s / (2 * centre)) * exp(-(dr * dr + dy * dy) / (core * core)) * (1 - exp(-y_ground / (rotor_ring_ground * rotor_radius)));
    }
    return stream;
}

float2 rotor_frame_velocity(RotorFlow flow, float s, float z, float y_ground)
{
    const float ds = 0.25;
    const float dy = 0.1;
    float low = max(s - ds, 0);
    float vertical = (rotor_stream(flow, low + 2 * ds, z, y_ground) - rotor_stream(flow, low, z, y_ground)) / ds;
    float down = max(y_ground - dy, 0);
    float span = (y_ground + dy) - down;
    float gradient = (rotor_stream(flow, s, z - dy, y_ground + dy) - rotor_stream(flow, s, z + (y_ground - down), down)) / span;
    float r = sqrt(s);
    return float2(r > 1e-3 ? -gradient / r : 0, vertical);
}

// Kept in step with rotor_air in src/simulation/rotorcraft.cpp: the induced flow only (no wind, no hub motion).
float3 rotor_air(RotorFlow flow, float3 position, float terrain_height)
{
    if (!(flow.inflow > 0)) return 0;
    float z = flow.hub.y - position.y;
    float y_ground = max(position.y - terrain_height, 0);
    float2 offset = position.xz - (flow.hub.xz + flow.skew * rotor_skew_offset(z));
    float s = dot(offset, offset);
    float r = sqrt(s);
    float window = smoothstep(-rotor_window_top_end * rotor_radius, -rotor_window_top_start * rotor_radius, z) *
        (1 - smoothstep(rotor_window_side_start * rotor_radius, rotor_window_side_end * rotor_radius, r));
    if (window <= 0) return 0;
    float2 frame_velocity = rotor_frame_velocity(flow, s, z, y_ground);
    float2 radial = r > 1e-3 ? offset / r : float2(0, 0);
    float3 air = float3(frame_velocity.x * radial.x, frame_velocity.y, frame_velocity.x * radial.y) * window;
    float speed_squared = dot(air, air);
    if (speed_squared > rotor_speed_limit * rotor_speed_limit) air *= rotor_speed_limit * rsqrt(speed_squared);
    return air;
}
