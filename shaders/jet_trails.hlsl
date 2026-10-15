#define PYROCLAST_REALISM
#define PYROCLAST_ROTOR
#include "frame_constants.hlsli"
#include "jet_noise.hlsli"

static const uint trail_segments = 33;

struct TrailInput
{
    float4 position : SV_Position;
    float3 world : TEXCOORD0;
    float3 shape : TEXCOORD1;
};

float condensation(float gamma, float core, float age)
{
    float humidity = asfloat(frame.blast_state.y);
    float swirl = 0.638 * gamma / (6.2831853 * max(core, 0.05));
    float raised = humidity + jet_humidity_per_kelvin * swirl * swirl / jet_air_heat_capacity;
    return saturate((raised - jet_condensation_humidity) / (1 - jet_condensation_humidity)) * exp(-age / jet_condensation_seconds);
}

TrailInput vs_trails(uint vertex : SV_VertexID)
{
    uint leg = vertex / (trail_segments * 6);
    uint segment = (vertex / 6) % trail_segments;
    uint corner = vertex % 6;
    uint count = min(frame.jet_state.x, jet_knot_limit);
    uint step = frame.jet_state.z;
    float4 head_left = frame.jet_heads[step * 4];
    float4 head_right = frame.jet_heads[step * 4 + 1];
    float fade = asfloat(frame.jet_state.w);
    float3 a;
    float3 b;
    float4 strength = 0;
    float2 ages = 0;
    bool valid = false;
    uint word = leg == 0 ? 0 : 1;
    if (segment == 0)
    {
        if (head_left.w > 0 && frame.jet_state.y != 0 && count != 0)
        {
            float3 centre = 0.5 * (head_left.xyz + head_right.xyz);
            float3 span = normalize(head_right.xyz - head_left.xyz);
            a = centre + span * (leg == 0 ? -0.5 : 0.5) * jet_span;
            b = frame.jet_knots[word].xyz;
            ages = float2(0, frame.jet_knots[3].y);
            strength.x = condensation(head_left.w, head_right.w, 0) * fade;
            strength.y = condensation(frame.jet_knots[0].w, frame.jet_knots[1].w, ages.y);
            valid = true;
        }
    }
    else
    {
        uint newer = segment - 1;
        if (newer + 1 < count && frame.jet_knots[newer * 4 + 3].x != 0)
        {
            a = frame.jet_knots[newer * 4 + word].xyz;
            b = frame.jet_knots[(newer + 1) * 4 + word].xyz;
            ages = float2(frame.jet_knots[newer * 4 + 3].y, frame.jet_knots[(newer + 1) * 4 + 3].y);
            strength.x = condensation(frame.jet_knots[newer * 4].w, frame.jet_knots[newer * 4 + 1].w, ages.x);
            strength.y = condensation(frame.jet_knots[(newer + 1) * 4].w, frame.jet_knots[(newer + 1) * 4 + 1].w, ages.y);
            valid = true;
        }
    }
    TrailInput output = (TrailInput)0;
    if (!valid || max(strength.x, strength.y) <= 0)
    {
        output.position = float4(0, 0, 0, 1);
        return output;
    }
    bool far_end = ((0x16u >> corner) & 1) != 0;
    float side_sign = ((0x34u >> corner) & 1) != 0 ? 1 : -1;
    float3 at = far_end ? b : a;
    float age = far_end ? ages.y : ages.x;
    float width = 0.18 + 0.22 * age;
    float3 along = b - a;
    float3 side = cross(along, at - frame.camera_position.xyz);
    float side_length = length(side);
    side = side_length > 1e-6 ? side / side_length : float3(0, 1, 0);
    output.world = at + side * (side_sign * width);
    output.position = mul(frame.view_projection, float4(output.world, 1));
    output.shape = float3(far_end ? strength.y : strength.x, side_sign, age);
    return output;
}

float4 ps_trails(TrailInput input) : SV_Target
{
    float across = input.shape.y;
    float mottle = jet_noise(input.world * 1.5 + float3(0, float(frame.clock.x) * 0.02, 0), 0x6a09e667u);
    float alpha = saturate(input.shape.x * (1 - across * across) * lerp(0.5, 1.2, mottle)) * 0.85;
    float3 ray = normalize(input.world - frame.camera_position.xyz);
    float3 color = jet_cloud_color(ray);
    float visibility = exp(-frame.fog.a * distance(input.world, frame.camera_position.xyz));
    return float4(color * alpha * visibility + frame.fog.rgb * (alpha * (1 - visibility)), alpha);
}
