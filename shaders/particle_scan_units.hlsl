#define PYROCLAST_REALISM
#include "realism.hlsli"

struct Parameters
{
    uint group_count;
    uint tick;
    uint layered;
    uint flags;
};
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<Parameters> parameters : register(b0);
ByteAddressBuffer alive_masks : register(t0);
ByteAddressBuffer classes : register(t1);
RWByteAddressBuffer offsets : register(u0);
RWByteAddressBuffer arguments : register(u1);

groupshared uint group_values[maximum_unit_groups];
groupshared uint3 sums[scan_threads];

static const uint category_shift = 30;
static const uint count_mask = (1u << category_shift) - 1;
static const uint category_light = 0;
static const uint category_smoke = 1;
static const uint category_debris = 2;
static const uint category_residual = 3;

uint group_category(uint group)
{
    Unit unit = find_unit(group);
    if (unit_is_cluster(unit)) return category_residual;
    uint slot = min(unit.id, maximum_effects - 1);
    uint layer_index = group_layer(classes, frame.effect_states[slot].w, group - unit.first_group);
    return layer_index == layer_debris ? category_debris : smoke_like(layer_index) ? category_smoke : category_light;
}

uint3 category_vector(uint category, uint count)
{
    bool smoke = category == category_smoke || category == category_residual;
    return uint3(smoke ? count : 0, category == category_light ? count : 0, category == category_debris ? count : 0);
}

[numthreads(scan_threads, 1, 1)]
void cs_main(uint thread : SV_GroupIndex)
{
    uint group_count = min(parameters.group_count, maximum_unit_groups);
    for (uint loaded = thread; loaded < group_count; loaded += scan_threads)
    {
        uint2 bits = alive_masks.Load2(loaded * 8);
        group_values[loaded] = (countbits(bits.x) + countbits(bits.y)) | (group_category(loaded) << category_shift);
    }
    GroupMemoryBarrierWithGroupSync();
    uint run = (group_count + scan_threads - 1) / scan_threads;
    uint first = min(thread * run, group_count);
    uint last = min(first + run, group_count);
    uint3 total = uint3(0, 0, 0);
    uint residual = 0;
    for (uint group = first; group < last; ++group)
    {
        uint category = group_values[group] >> category_shift;
        uint count = group_values[group] & count_mask;
        total += category_vector(category, count);
        if (category == category_residual) residual += count;
    }
    sums[thread] = total;
    GroupMemoryBarrierWithGroupSync();
    for (uint span = 1; span < scan_threads; span *= 2)
    {
        uint3 earlier = thread >= span ? sums[thread - span] : uint3(0, 0, 0);
        GroupMemoryBarrierWithGroupSync();
        sums[thread] += earlier;
        GroupMemoryBarrierWithGroupSync();
    }
    uint3 before = sums[thread] - total;
    for (uint placed = first; placed < last; ++placed)
    {
        uint category = group_values[placed] >> category_shift;
        uint count = group_values[placed] & count_mask;
        bool smoke = category == category_smoke || category == category_residual;
        uint translucent = parameters.layered != 0 ? (smoke ? before.x : before.y) : before.x + before.y;
        group_values[placed] = category == category_debris ? before.z : translucent;
        before += category_vector(category, count);
    }
    uint3 alive = sums[scan_threads - 1];
    GroupMemoryBarrierWithGroupSync();
    for (uint stored = thread; stored < group_count; stored += scan_threads)
    {
        offsets.Store(stored * 4, group_values[stored]);
    }
    sums[thread] = uint3(residual, 0, 0);
    GroupMemoryBarrierWithGroupSync();
    for (uint half_span = scan_threads / 2; half_span > 0; half_span /= 2)
    {
        if (thread < half_span) sums[thread].x += sums[thread + half_span].x;
        GroupMemoryBarrierWithGroupSync();
    }
    if (thread == 0)
    {
        arguments.Store4(particle_arguments_offset, uint4(particle_vertices, alive.x + alive.y, 0, 0));
        arguments.Store4(smoke_arguments_offset, uint4(particle_vertices, alive.x, 0, 0));
        arguments.Store4(light_arguments_offset, uint4(particle_vertices, alive.y, 0, 0));
        arguments.Store4(debris_arguments_offset, uint4(debris_vertices, alive.z, 0, 0));
        arguments.Store4(counters_offset, uint4(parameters.tick, alive.x + alive.y + alive.z, alive.x, alive.y));
        arguments.Store4(counters_offset + 16, uint4(alive.z, 0, 0, 0));
        bool measure = (parameters.flags & listing_measure) != 0;
        if (measure) arguments.Store4(counters_offset + 32, uint4(0, 0, 0, 0));
        if (measure) arguments.Store(counters_offset + counter_light_coverage_word * 4, 0);
        arguments.Store2(counters_offset + counter_residual_word * 4,
            uint2(sums[0].x, counter_flag_realism | (measure ? counter_flag_measured : 0u)));
    }
}
