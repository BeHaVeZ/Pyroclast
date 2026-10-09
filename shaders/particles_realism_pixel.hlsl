#define PYROCLAST_REALISM
#include "particles.hlsl"
#include "particle_realism_vertex.hlsli"

float2 billow_gradient(int2 cell, uint salt)
{
    uint value = uint(cell.x) * 73856093u ^ uint(cell.y) * 19349663u ^ salt * 2654435761u;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    return float2(uint2(value, value >> 16) & 0xffffu) * (2.0 / 65535.0) - 1.0;
}

float3 billow_noise(float2 position, uint salt)
{
    float2 floor_position = floor(position);
    int2 cell = int2(floor_position);
    float2 w = position - floor_position;
    float2 u = w * w * w * (w * (w * 6 - 15) + 10);
    float2 du = 30 * w * w * (w * (w - 2) + 1);
    float2 ga = billow_gradient(cell, salt);
    float2 gb = billow_gradient(cell + int2(1, 0), salt);
    float2 gc = billow_gradient(cell + int2(0, 1), salt);
    float2 gd = billow_gradient(cell + int2(1, 1), salt);
    float va = dot(ga, w);
    float vb = dot(gb, w - float2(1, 0));
    float vc = dot(gc, w - float2(0, 1));
    float vd = dot(gd, w - float2(1, 1));
    float value = va + u.x * (vb - va) + u.y * (vc - va) + u.x * u.y * (va - vb - vc + vd);
    float2 gradient = ga + u.x * (gb - ga) + u.y * (gc - ga) + u.x * u.y * (ga - gb - gc + gd) +
        du * (float2(vb, vc) - va + u.yx * (va - vb - vc + vd));
    return float3(gradient, value);
}

float4 billowed(RealismPixelInput realism)
{
    PixelInput input = realism.base;
    float radius = dot(input.uv, input.uv);
    if (radius >= 1) discard;
    uint2 depth_size;
    scene_depth.GetDimensions(depth_size.x, depth_size.y);
    uint2 texel = min(uint2(input.position.xy * draw.depth_scale), depth_size - 1);
    float scene = frame.camera_position.w / max(scene_depth.Load(int3(texel, 0)), 1e-9);
    float fade = saturate((scene - input.distance) / input.softness);
    float height = sqrt(saturate(1 - radius));
    float3 noise = billow_noise(realism.detail.xy + input.uv * (realism.detail.z * (1 - 0.25 * height)), 0x5eedu);
    float ramp = saturate(realism.detail.w / 0.55);
    float profile = saturate((1 - radius) + realism.detail.w * 1.4 * noise.z - 0.1 * ramp) * (1 + 0.25 * ramp);
    float alpha = saturate(profile * input.color.a) * fade;
    float3 color = input.color.rgb;
    if (input.shaded > 0.5)
    {
        float3 normal = float3(input.uv, height);
        normal.xy -= 0.35 * realism.detail.w * noise.xy;
        normal = normalize(normal);
        float sun = saturate((dot(normal, frame.sun_billboard.xyz) + 0.6) / 1.6) * input.lighting.w;
        float3 sky = lerp(frame.ground_color.rgb, frame.sky_color.rgb, dot(normal, frame.up_billboard.xyz) * 0.5 + 0.5);
        float3 ambient = lerp(sky, dot(sky, 1.0 / 3).xxx, 0.5) + frame.sun_color.rgb * 0.1;
        color = color * (frame.sun_color.rgb * sun + ambient) + input.lighting.rgb;
    }
    return float4(color * alpha, alpha * input.occlusion);
}

float4 ps_realism(RealismPixelInput realism) : SV_Target
{
    PixelInput input = realism.base;
    float4 color = realism.detail.w > 0 ? billowed(realism) : ps_main(input);
    if (!mask_enabled(frame.light_state.y, haze_mask_bit)) return color;
    float2 offset = input.position.xy * draw.depth_scale - 0.5 * frame.viewport.xy;
    float distance = input.distance * sqrt(1 + dot(offset, offset) / (frame.projection.x * frame.projection.x));
    float visibility = exp(-frame.fog.a * distance);
    return float4(color.rgb * visibility + frame.fog.rgb * (color.a * (1 - visibility)), color.a);
}
