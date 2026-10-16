#pragma once

RealismPixelInput rotor_disc_vertex(uint vertex, uint disc)
{
    const float2 corners[6] = {
        float2(-1, 1), float2(1, 1), float2(1, -1),
        float2(-1, 1), float2(1, -1), float2(-1, -1)
    };
    float4 centre = frame.rotor_discs[disc * 3];
    float4 along = frame.rotor_discs[disc * 3 + 1];
    float4 across = frame.rotor_discs[disc * 3 + 2];
    float fade = asfloat(frame.rotor_state.w);
    float2 corner = corners[vertex] * 1.02;
    float3 world = centre.xyz + (corner.x * along.xyz + corner.y * across.xyz) * centre.w;
    PixelInput output = (PixelInput)0;
    output.position = mul(frame.view_projection, float4(world, 1));
    output.distance = output.position.w;
    output.softness = 0.1;
    output.uv = corner;
    float3 normal = normalize(cross(along.xyz, across.xyz));
    float sun = abs(dot(normal, frame.sun_direction.xyz));
    float3 sky = lerp(frame.ground_color.rgb, frame.sky_color.rgb, 0.5 + 0.5 * abs(normal.y));
    float3 light = frame.sun_color.rgb * sun + lerp(sky, dot(sky, 1.0 / 3).xxx, 0.5) + realism_point_lights(centre.xyz);
    output.color = float4(light, fade);
    output.occlusion = 1;
    RealismPixelInput result;
    result.base = output;
    result.detail = float4(float(disc), along.w, -1, across.w);
    if (fade <= 0 || centre.w <= 0) result.base.position = float4(0, 0, 0, 1);
    return result;
}

RealismPixelInput rotor_vertex(uint vertex, uint record)
{
    uint local = record % explosion_particles;
    if (load_effect_residual(record / explosion_particles).rotor_disc) return rotor_disc_vertex(vertex, min(local, rotor_disc_records - 1));
    RealismFootprint footprint = rotor_footprint(particles, classes, record);
    RealismPixelInput result = realism_vertex(vertex, footprint);
    if (footprint.footprint.opacity <= 0) result.base.position = float4(0, 0, 0, 1);
    return result;
}

// Derivatives are taken before the discard.
float4 rotor_disc_pixel(RealismPixelInput realism)
{
    PixelInput input = realism.base;
    float rho = length(input.uv);
    float pixel = max(abs(ddx_fine(rho)) + abs(ddy_fine(rho)), 1e-4);
    if (rho >= 1.02) discard;
    uint disc = uint(realism.detail.x + 0.5);
    float radius = frame.rotor_discs[disc * 3].w;
    float metres = max(rho * radius, 1e-3);
    float chord = disc == 0 ? rotor_chord : tail_rotor_chord;
    float width = chord / metres;
    float spacing = 6.2831853 / float(rotor_blades);
    float half_width = clamp(realism.detail.w, 0.5 * width, spacing);
    float theta = atan2(input.uv.y, input.uv.x);
    float coverage = 0;
    [unroll] for (uint blade = 0; blade < rotor_blades; ++blade)
    {
        float centre = realism.detail.y + float(blade) * spacing - half_width;
        float offset = theta - centre;
        offset -= 6.2831853 * floor(offset / 6.2831853 + 0.5);
        coverage += (width / half_width) * saturate(1 - abs(offset) / half_width);
    }
    float rim = 1 - smoothstep(1 - max(pixel, 0.005), 1, rho);
    float hub = saturate(metres / 0.15);
    float alpha = saturate(coverage) * rim * hub * input.color.a;
    uint2 depth_size;
    scene_depth.GetDimensions(depth_size.x, depth_size.y);
    uint2 texel = min(uint2(input.position.xy * draw.depth_scale), depth_size - 1);
    float scene = frame.camera_position.w / max(scene_depth.Load(int3(texel, 0)), 1e-9);
    alpha *= saturate((scene - input.distance) / input.softness);
    float albedo = lerp(0.06, 0.5, smoothstep(0.96 - 0.5 * pixel, 0.96 + 0.5 * pixel, rho));
    float3 color = input.color.rgb * albedo;
    float2 offset = input.position.xy * draw.depth_scale - 0.5 * frame.viewport.xy;
    float distance = input.distance * sqrt(1 + dot(offset, offset) / (frame.projection.x * frame.projection.x));
    float visibility = exp(-frame.fog.a * distance);
    return float4((color * visibility + frame.fog.rgb * (1 - visibility)) * alpha, alpha);
}
