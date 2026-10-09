#define PYROCLAST_REALISM
#include "particles.hlsl"
#include "realism_layout.hlsli"

float4 ps_realism(PixelInput input) : SV_Target
{
    float4 color = ps_main(input);
    if (!mask_enabled(frame.light_state.y, haze_mask_bit)) return color;
    float2 offset = input.position.xy * draw.depth_scale - 0.5 * frame.viewport.xy;
    float distance = input.distance * sqrt(1 + dot(offset, offset) / (frame.projection.x * frame.projection.x));
    float visibility = exp(-frame.fog.a * distance);
    return float4(color.rgb * visibility + frame.fog.rgb * (color.a * (1 - visibility)), color.a);
}
