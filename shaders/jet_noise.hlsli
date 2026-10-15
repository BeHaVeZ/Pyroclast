#pragma once

float jet_hash(float3 cell, uint seed)
{
    uint3 q = uint3(int3(floor(cell)) + 65536);
    uint h = q.x * 0x8da6b343u ^ q.y * 0xd8163841u ^ q.z * 0xcb1ab31fu ^ seed;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return float(h & 0xffffu) / 65535.0;
}

float jet_noise(float3 p, uint seed)
{
    float3 cell = floor(p);
    float3 f = p - cell;
    float3 s = f * f * (3 - 2 * f);
    float n000 = jet_hash(cell, seed);
    float n100 = jet_hash(cell + float3(1, 0, 0), seed);
    float n010 = jet_hash(cell + float3(0, 1, 0), seed);
    float n110 = jet_hash(cell + float3(1, 1, 0), seed);
    float n001 = jet_hash(cell + float3(0, 0, 1), seed);
    float n101 = jet_hash(cell + float3(1, 0, 1), seed);
    float n011 = jet_hash(cell + float3(0, 1, 1), seed);
    float n111 = jet_hash(cell + float3(1, 1, 1), seed);
    return lerp(lerp(lerp(n000, n100, s.x), lerp(n010, n110, s.x), s.y), lerp(lerp(n001, n101, s.x), lerp(n011, n111, s.x), s.y), s.z);
}

float jet_phase(float g, float cosine)
{
    return (1 - g * g) / pow(max(1 + g * g - 2 * g * cosine, 1e-4), 1.5);
}

float3 jet_cloud_color(float3 ray)
{
    float3 ambient = 0.5 * (frame.sky_color.rgb + frame.horizon_color.rgb);
    ambient = lerp(ambient, dot(ambient, 1.0 / 3).xxx, 0.5);
    float cosine = dot(ray, frame.sun_direction.xyz);
    float phase = 0.8 * jet_phase(0.6, cosine) + 0.2 * jet_phase(-0.3, cosine);
    return 0.95 * (ambient + 0.3 * frame.sun_color.rgb * phase);
}
