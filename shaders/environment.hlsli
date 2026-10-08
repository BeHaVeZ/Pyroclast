#pragma once

#include "terrain.hlsli"

struct PropBox
{
    float3 centre;
    float2 axis;
    float3 half_extent;
};

PropBox load_prop_box(ByteAddressBuffer environment, uint index)
{
    uint address = environment_boxes_offset + min(index, maximum_prop_boxes - 1) * prop_box_stride;
    float4 first = asfloat(environment.Load4(address));
    float4 second = asfloat(environment.Load4(address + 16));
    PropBox box;
    box.centre = first.xyz;
    box.axis = float2(first.w, second.w);
    box.half_extent = second.xyz;
    return box;
}

float3 to_box(PropBox box, float3 offset)
{
    return float3(offset.x * box.axis.x + offset.z * box.axis.y, offset.y, offset.z * box.axis.x - offset.x * box.axis.y);
}

float3 from_box(PropBox box, float3 local)
{
    return float3(local.x * box.axis.x - local.z * box.axis.y, local.y, local.x * box.axis.y + local.z * box.axis.x);
}

struct PropCell
{
    uint2 mask;
    float ceiling;
};

PropCell load_prop_cell(ByteAddressBuffer environment, float2 xz)
{
    PropCell cell;
    cell.mask = uint2(0, 0);
    cell.ceiling = -1e30;
    float2 index = floor((xz - environment_grid_min) * environment_cell_inverse);
    if (all(index >= 0) && all(index < float(environment_grid_cells)))
    {
        uint2 column_row = uint2(index);
        uint4 record = environment.Load4(environment_grid_offset +
            (column_row.y * environment_grid_cells + column_row.x) * environment_cell_bytes);
        cell.mask = record.xy;
        cell.ceiling = asfloat(record.z);
    }
    return cell;
}

uint2 load_open_bottoms(ByteAddressBuffer environment)
{
    return environment.Load2(environment_flags_offset);
}

float footprint_distance(PropBox box, float2 xz)
{
    float3 local = to_box(box, float3(xz.x - box.centre.x, 0, xz.y - box.centre.z));
    return length(max(abs(local.xz) - box.half_extent.xz, 0));
}

float cover_shade(PropBox box, float3 position)
{
    if (position.y >= box.centre.y - box.half_extent.y) return 1;
    return lerp(0.35, 1, saturate(footprint_distance(box, position.xz) * 2));
}

float prop_ground_shade(ByteAddressBuffer environment, float3 world)
{
    PropCell cell = load_prop_cell(environment, world.xz);
    if (all(cell.mask == 0)) return 1;
    uint2 open = load_open_bottoms(environment);
    float halo = 1;
    float cover = 1;
    for (uint word = 0; word < 2; ++word)
    {
        uint bits = word == 0 ? cell.mask.x : cell.mask.y;
        uint open_bits = word == 0 ? open.x : open.y;
        [loop] while (bits != 0)
        {
            uint bit = firstbitlow(bits);
            bits &= bits - 1;
            PropBox box = load_prop_box(environment, word * 32 + bit);
            if (((open_bits >> bit) & 1) != 0)
            {
                cover = min(cover, cover_shade(box, world));
            }
            else if (box.centre.y - box.half_extent.y <= 0)
            {
                float near = 1 - saturate(footprint_distance(box, world.xz) / 1.2);
                halo *= 1 - 0.45 * near * near;
            }
        }
    }
    return halo * cover;
}

float prop_cover_shade(ByteAddressBuffer environment, float3 world, float3 normal)
{
    float3 probe = world + normal * 0.7;
    PropCell cell = load_prop_cell(environment, probe.xz);
    uint2 open = load_open_bottoms(environment) & cell.mask;
    float cover = 1;
    for (uint word = 0; word < 2; ++word)
    {
        uint bits = word == 0 ? open.x : open.y;
        [loop] while (bits != 0)
        {
            uint bit = firstbitlow(bits);
            bits &= bits - 1;
            cover = min(cover, cover_shade(load_prop_box(environment, word * 32 + bit), probe));
        }
    }
    return cover;
}
