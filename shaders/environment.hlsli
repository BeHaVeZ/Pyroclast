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

// Twins of to_box/from_box in src/simulation/range_props.cpp.
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

// Kept in step with Environment::cell; empty outside the grid and for NaN.
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

uint2 nearby_boxes(ByteAddressBuffer environment, float3 position, bool cull)
{
    PropCell cell = load_prop_cell(environment, position.xz);
    return cull && position.y > cell.ceiling ? uint2(0, 0) : cell.mask;
}

float component(float3 v, uint axis)
{
    return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}

float3 with_component(float3 v, uint axis, float value)
{
    return float3(axis == 0 ? value : v.x, axis == 1 ? value : v.y, axis == 2 ? value : v.z);
}

bool slab_entry(float3 start, float3 motion, float3 extent, out float enter, out uint axis)
{
    enter = -1;
    axis = 3;
    float leave = 1;
    [unroll] for (uint a = 0; a < 3; ++a)
    {
        float m = component(motion, a);
        float o = component(start, a);
        float e = component(extent, a);
        if (abs(m) < 1e-6)
        {
            if (abs(o) >= e) return false;
            continue;
        }
        float toward = m > 0 ? 1 : -1;
        float near_t = (-toward * e - o) / m;
        float far_t = (toward * e - o) / m;
        if (near_t > enter)
        {
            enter = near_t;
            axis = a;
        }
        leave = min(leave, far_t);
    }
    return axis < 3 && enter >= 0 && enter <= 1 && enter <= leave;
}

// Returns the entered face's normal, or zero (an exact no-op) when the box is not touched.
float3 prop_contact(PropBox box, bool open_bottom, float3 start, inout float3 end, inout float3 velocity, float radius,
    float restitution, float friction)
{
    float3 extent = box.half_extent + radius;
    float3 from = to_box(box, start - box.centre);
    float3 to = to_box(box, end - box.centre);
    uint axis;
    float side;
    if (all(abs(from) < extent))
    {
        if (!all(abs(to) < extent)) return float3(0, 0, 0);
        float3 depth = extent - abs(from);
        float3 sides = float3(from.x >= 0 ? 1 : -1, from.y >= 0 ? 1 : -1, from.z >= 0 ? 1 : -1);
        if (!open_bottom && from.y < 0)
        {
            depth.y = extent.y - from.y;
            sides.y = 1;
        }
        axis = depth.x <= depth.y && depth.x <= depth.z ? 0 : depth.y <= depth.z ? 1 : 2;
        side = component(sides, axis);
    }
    else
    {
        float enter;
        if (!slab_entry(from, to - from, extent, enter, axis)) return float3(0, 0, 0);
        side = component(to - from, axis) > 0 ? -1 : 1;
    }
    end = box.centre + from_box(box, with_component(to, axis, side * component(extent, axis)));
    float3 normal = from_box(box, with_component(float3(0, 0, 0), axis, side));
    float inward = dot(velocity, normal);
    if (inward < 0)
    {
        float3 along = velocity - normal * inward;
        velocity = restitution > 0 ? along * friction - normal * (inward * restitution) : along;
    }
    return normal;
}

// -1e30 when `position` is not over the box's top grown by `radius`.
float prop_support(PropBox box, float3 position, float radius)
{
    float3 local = to_box(box, position - box.centre);
    bool over = abs(local.x) <= box.half_extent.x + radius && abs(local.z) <= box.half_extent.z + radius && local.y >= box.half_extent.y;
    return over ? box.centre.y + box.half_extent.y : -1e30;
}

float prop_floor(ByteAddressBuffer environment, float3 position)
{
    float floor_height = terrain_height(environment, position.xz);
    uint2 boxes = load_prop_cell(environment, position.xz).mask;
    for (uint word = 0; word < 2; ++word)
    {
        uint bits = word == 0 ? boxes.x : boxes.y;
        [loop] while (bits != 0)
        {
            uint bit = firstbitlow(bits);
            bits &= bits - 1;
            floor_height = max(floor_height, prop_support(load_prop_box(environment, word * 32 + bit), position, 0));
        }
    }
    return floor_height;
}

float3 prop_clip_spawn(ByteAddressBuffer environment, float3 origin, float3 spawn)
{
    uint2 boxes = load_prop_cell(environment, origin.xz).mask | load_prop_cell(environment, spawn.xz).mask;
    float3 offset = spawn - origin;
    float distance = length(offset);
    if (all(boxes == 0) || distance < 1e-6) return spawn;
    float earliest = 1;
    for (uint word = 0; word < 2; ++word)
    {
        uint bits = word == 0 ? boxes.x : boxes.y;
        [loop] while (bits != 0)
        {
            uint bit = firstbitlow(bits);
            bits &= bits - 1;
            PropBox box = load_prop_box(environment, word * 32 + bit);
            float3 from = to_box(box, origin - box.centre);
            if (all(abs(from) < box.half_extent)) continue;
            float enter;
            uint axis;
            if (slab_entry(from, to_box(box, offset), box.half_extent, enter, axis)) earliest = min(earliest, enter);
        }
    }
    return earliest >= 1 ? spawn : origin + offset * max(earliest - prop_spawn_backoff / distance, 0);
}

// An exact no-op for a box beyond radius + deflection_band.
float3 deflect_air(ByteAddressBuffer environment, uint2 boxes, float3 position, float radius, float3 air)
{
    for (uint word = 0; word < 2; ++word)
    {
        uint bits = word == 0 ? boxes.x : boxes.y;
        [loop] while (bits != 0)
        {
            uint bit = firstbitlow(bits);
            bits &= bits - 1;
            PropBox box = load_prop_box(environment, word * 32 + bit);
            float3 local = to_box(box, position - box.centre);
            float3 beyond = abs(local) - box.half_extent;
            float3 outside = max(beyond, 0);
            float distance = length(outside);
            if (distance >= radius + deflection_band) continue;
            float3 away;
            if (distance > 1e-4)
            {
                away = float3(local.x >= 0 ? outside.x : -outside.x, local.y >= 0 ? outside.y : -outside.y,
                    local.z >= 0 ? outside.z : -outside.z) / distance;
            }
            else
            {
                uint axis = beyond.x >= beyond.y && beyond.x >= beyond.z ? 0 : beyond.y >= beyond.z ? 1 : 2;
                away = with_component(float3(0, 0, 0), axis, component(local, axis) >= 0 ? 1 : -1);
            }
            float3 normal = from_box(box, away);
            float weight = saturate(1 - max(distance - radius, 0) / deflection_band);
            float into = dot(air, normal);
            if (into < 0)
            {
                air -= normal * (into * weight);
                air.y -= into * weight * deflection_lift * (1 - abs(normal.y));
            }
        }
    }
    return air;
}
