#include "frame_constants.hlsli"
#include "terrain.hlsli"

struct Mark
{
    uint kind;
    uint segments;
    float radius;
    float lift;
    float3 centre;
    float half_width;
    float3 axis;
    uint side_segments;
    float3 colour;
    float reserved;
};
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<Mark> mark : register(b0);
ByteAddressBuffer environment : register(t0);

static const uint ring = 0;
static const uint draped_circle = 1;
static const uint draped_rectangle = 2;
static const uint wind_arrow = 3;

struct PixelInput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

float3 on_terrain(float2 xz)
{
    return float3(xz.x, terrain_height(environment, xz) + mark.lift, xz.y);
}

float3 outline_point(uint i)
{
    float segments = float(max(mark.segments, 1u));
    if (mark.kind == ring || mark.kind == draped_circle)
    {
        float angle = 6.2831853 * float(i) / segments;
        if (mark.kind == draped_circle) return on_terrain(mark.centre.xz + float2(cos(angle), sin(angle)) * mark.radius);
        float3 axis = normalize(mark.axis);
        float3 u = normalize(cross(abs(axis.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0), axis));
        float3 v = cross(axis, u);
        return mark.centre + (u * cos(angle) + v * sin(angle)) * mark.radius;
    }
    uint half_count = max(mark.segments / 2, 2u);
    uint long_pieces = clamp(mark.side_segments, 1u, half_count - 1);
    uint short_pieces = half_count - long_pieces;
    float a = mark.radius;
    float w = mark.half_width;
    float2 local;
    if (i <= long_pieces) local = float2(lerp(-a, a, float(i) / float(long_pieces)), -w);
    else if (i <= long_pieces + short_pieces) local = float2(a, lerp(-w, w, float(i - long_pieces) / float(short_pieces)));
    else if (i <= 2 * long_pieces + short_pieces) local = float2(lerp(a, -a, float(i - long_pieces - short_pieces) / float(long_pieces)), w);
    else local = float2(-a, lerp(w, -w, float(min(i - 2 * long_pieces - short_pieces, short_pieces)) / float(short_pieces)));
    float2 along = normalize(mark.axis.xz);
    float2 across = float2(-along.y, along.x);
    return on_terrain(mark.centre.xz + along * local.x + across * local.y);
}

PixelInput vs_main(uint vertex : SV_VertexID)
{
    PixelInput output = (PixelInput)0;
    output.color = mark.colour;
    float3 position;
    if (mark.kind == wind_arrow)
    {
        float3 top = mark.centre + float3(0, mark.lift, 0);
        float3 drift = frame.wind.xyz * 2;
        float speed = length(drift);
        float3 direction = speed > 1e-3 ? drift / speed : float3(1, 0, 0);
        float3 side = float3(-direction.z, 0, direction.x);
        float head = max(1.0, 0.2 * speed);
        float3 tip = top + drift;
        uint piece = min(vertex / 2, 3u);
        bool far_end = (vertex & 1) != 0;
        float3 stroke = tip - direction * head + side * (piece == 2 ? 0.5 * head : -0.5 * head);
        float3 start = piece == 0 ? mark.centre : piece == 1 ? top : tip;
        float3 finish = piece == 0 ? top : piece == 1 ? tip : stroke;
        position = far_end ? finish : start;
    }
    else if (mark.kind == ring && vertex >= 2 * mark.segments)
    {
        position = mark.centre + normalize(mark.axis) * (vertex == 2 * mark.segments ? 0.0 : 2.0);
    }
    else
    {
        uint segment = min(vertex / 2, max(mark.segments, 1u) - 1);
        position = outline_point(segment + (vertex & 1));
    }
    output.position = mul(frame.view_projection, float4(position, 1));
    return output;
}
