#pragma once

#include "realism.hlsli"
#include "blast_list.hlsli"

struct BlastKnot
{
    uint radius_cm;
    uint arrival_q16;
    uint ticks_per_cm_q24;
    float overpressure;
};

uint blast_address(uint effect_class)
{
    return effect_class_address(effect_class) + class_blast_offset;
}

BlastKnot load_blast_knot(ByteAddressBuffer classes, uint effect_class, uint index)
{
    uint4 words = classes.Load4(blast_address(effect_class) + index * 16);
    BlastKnot knot;
    knot.radius_cm = words.x;
    knot.arrival_q16 = words.y;
    knot.ticks_per_cm_q24 = words.z;
    knot.overpressure = asfloat(words.w);
    return knot;
}

float4 load_blast_skirt(ByteAddressBuffer classes, uint effect_class)
{
    return asfloat(classes.Load4(blast_address(effect_class) + 272));
}

uint blast_segment(ByteAddressBuffer classes, uint effect_class, uint radius_cm)
{
    uint index = 0;
    for (uint next = 1; next + 1 < blast_knots; ++next)
    {
        if (load_blast_knot(classes, effect_class, next).radius_cm <= radius_cm) index = next;
    }
    return index;
}

uint blast_arrival_tick(ByteAddressBuffer classes, uint effect_class, uint radius_cm)
{
    BlastKnot first = load_blast_knot(classes, effect_class, 0);
    if (radius_cm <= first.radius_cm) return 0;
    BlastKnot knot = load_blast_knot(classes, effect_class, blast_segment(classes, effect_class, radius_cm));
    uint arrival_q16 = knot.arrival_q16 + (((radius_cm - knot.radius_cm) * knot.ticks_per_cm_q24) >> 8);
    return (arrival_q16 + 0xffff) >> 16;
}

float blast_overpressure(ByteAddressBuffer classes, uint effect_class, float metres)
{
    float centimetres = metres * 100;
    uint index = blast_segment(classes, effect_class, uint(max(centimetres, 0)));
    BlastKnot near = load_blast_knot(classes, effect_class, index);
    BlastKnot far = load_blast_knot(classes, effect_class, index + 1);
    float near_radius = float(near.radius_cm);
    float exponent = log2(far.overpressure / near.overpressure) / log2(float(far.radius_cm) / near_radius);
    float clamped = max(centimetres, float(load_blast_knot(classes, effect_class, 0).radius_cm));
    return near.overpressure * exp2(exponent * log2(clamped / near_radius));
}

float3 blast_push(float3 position, uint mask, uint own_slot)
{
    float3 push = 0;
    if (!mask_enabled(mask, push_mask_bit)) return push;
    for (uint index = 0; index < blast_count(); ++index)
    {
        uint slot = blast_slot(index);
        if (slot == own_slot) continue;
        float before = frame.blast_fronts[index].x;
        float after = frame.blast_rings[index].x;
        float3 offset = position - frame.effect_origins[slot].xyz;
        float reach = length(offset);
        if (reach <= before || reach > after || reach < 1e-3) continue;
        float share = (reach - before) / max(after - before, 1e-3);
        push += offset / reach * lerp(frame.blast_fronts[index].y, frame.blast_fronts[index].z, share);
    }
    return push;
}

float blast_particle_velocity(float overpressure)
{
    return 340.0 / 1.4 * overpressure / sqrt(1 + 0.85714286 * overpressure);
}
