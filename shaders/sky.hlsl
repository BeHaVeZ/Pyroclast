#include "frame_constants.hlsli"

float4 vs_main(uint vertex : SV_VertexID) : SV_Position
{
    float2 uv = float2((vertex << 1) & 2, vertex & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 ps_main(float4 position : SV_Position) : SV_Target
{
    float2 ndc = position.xy * frame.viewport.zw * float2(2, -2) + float2(-1, 1);
    float3 direction = normalize(mul(frame.inverse_view_projection, float4(ndc, 0, 1)).xyz);
    float3 sky = lerp(frame.horizon_color.rgb, frame.sky_color.rgb, sqrt(saturate(direction.y)));
    sky = direction.y < 0 ? lerp(frame.horizon_color.rgb, frame.fog.rgb, saturate(-direction.y * 8)) : sky;
    float sun = saturate(dot(direction, frame.sun_direction.xyz));
    sky += frame.sun_color.rgb * (pow(sun, 1500) * 4 + pow(sun, 12) * 0.12);
    return float4(sky, 1);
}
