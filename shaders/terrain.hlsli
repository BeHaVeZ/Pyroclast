#pragma once

#include "terrain_layout.hlsli"

static const float terrain_half_extent = (terrain_resolution - 1) * terrain_spacing * 0.5;

float terrain_sample(ByteAddressBuffer heights, uint column, uint row)
{
    column = min(column, terrain_resolution - 1);
    row = min(row, terrain_resolution - 1);
    return asfloat(heights.Load((row * terrain_resolution + column) * 4));
}

float terrain_height(ByteAddressBuffer heights, float2 xz)
{
    float2 grid = clamp((xz + terrain_half_extent) / terrain_spacing, 0, float(terrain_resolution - 1));
    uint2 cell = min(uint2(grid), terrain_resolution - 2);
    float2 blend = grid - float2(cell);
    float near_row = lerp(terrain_sample(heights, cell.x, cell.y), terrain_sample(heights, cell.x + 1, cell.y), blend.x);
    float far_row = lerp(terrain_sample(heights, cell.x, cell.y + 1), terrain_sample(heights, cell.x + 1, cell.y + 1), blend.x);
    return lerp(near_row, far_row, blend.y);
}
