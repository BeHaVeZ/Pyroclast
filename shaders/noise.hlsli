#pragma once

uint noise_hash(int3 cell, uint salt)
{
    uint value = uint(cell.x) * 73856093u ^ uint(cell.y) * 19349663u ^ uint(cell.z) * 83492791u ^ salt * 2654435761u;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

float3 noise_gradient(int3 cell, uint salt)
{
    uint value = noise_hash(cell, salt);
    return float3(uint3(value, value >> 10, value >> 20) & 1023u) * (2.0 / 1023.0) - 1.0;
}

// After Inigo Quilez. xyz: the gradient at `position`; w: the value.
float4 gradient_noise(float3 position, uint salt)
{
    float3 floor_position = floor(position);
    int3 cell = int3(floor_position);
    float3 w = position - floor_position;
    float3 u = w * w * w * (w * (w * 6 - 15) + 10);
    float3 du = 30 * w * w * (w * (w - 2) + 1);
    float3 ga = noise_gradient(cell + int3(0, 0, 0), salt);
    float3 gb = noise_gradient(cell + int3(1, 0, 0), salt);
    float3 gc = noise_gradient(cell + int3(0, 1, 0), salt);
    float3 gd = noise_gradient(cell + int3(1, 1, 0), salt);
    float3 ge = noise_gradient(cell + int3(0, 0, 1), salt);
    float3 gf = noise_gradient(cell + int3(1, 0, 1), salt);
    float3 gg = noise_gradient(cell + int3(0, 1, 1), salt);
    float3 gh = noise_gradient(cell + int3(1, 1, 1), salt);
    float va = dot(ga, w - float3(0, 0, 0));
    float vb = dot(gb, w - float3(1, 0, 0));
    float vc = dot(gc, w - float3(0, 1, 0));
    float vd = dot(gd, w - float3(1, 1, 0));
    float ve = dot(ge, w - float3(0, 0, 1));
    float vf = dot(gf, w - float3(1, 0, 1));
    float vg = dot(gg, w - float3(0, 1, 1));
    float vh = dot(gh, w - float3(1, 1, 1));
    float k1 = va - vb - vc + vd;
    float k2 = va - vc - ve + vg;
    float k3 = va - vb - ve + vf;
    float k4 = -va + vb + vc - vd + ve - vf - vg + vh;
    float value = va + u.x * (vb - va) + u.y * (vc - va) + u.z * (ve - va) + u.x * u.y * k1 + u.y * u.z * k2 + u.z * u.x * k3 +
        u.x * u.y * u.z * k4;
    float3 gradient = ga + u.x * (gb - ga) + u.y * (gc - ga) + u.z * (ge - ga) + u.x * u.y * (ga - gb - gc + gd) +
        u.y * u.z * (ga - gc - ge + gg) + u.z * u.x * (ga - gb - ge + gf) + u.x * u.y * u.z * (-ga + gb + gc - gd + ge - gf - gg + gh) +
        du * (float3(vb, vc, ve) - va + u.yzx * float3(k1, k2, k3) + u.zxy * float3(k3, k1, k2) + u.yzx * u.zxy * k4);
    return float4(gradient, value);
}

float3 curl_noise(float3 position)
{
    float3 d1 = gradient_noise(position, 1).xyz;
    float3 d2 = gradient_noise(position, 2).xyz;
    float3 d3 = gradient_noise(position, 3).xyz;
    return float3(d3.y - d2.z, d1.z - d3.x, d2.x - d1.y);
}
