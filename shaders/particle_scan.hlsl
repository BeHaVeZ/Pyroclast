#include "effect_classes.hlsli"
#include "frame_constants.hlsli"

struct Parameters
{
    uint group_count;
    uint tick;
    uint layered;
};
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<Parameters> parameters : register(b0);
ByteAddressBuffer alive_masks : register(t0);
ByteAddressBuffer classes : register(t1);
RWByteAddressBuffer offsets : register(u0);
RWByteAddressBuffer arguments : register(u1);

static const uint maximum_groups = maximum_effects * slab_groups;
groupshared uint group_values[maximum_groups];
groupshared uint3 sums[scan_threads];

static const uint category_shift = 30;
static const uint count_mask = (1u << category_shift) - 1;
static const uint category_light = 0;
static const uint category_smoke = 1;
static const uint category_debris = 2;

uint group_category(uint group)
{
    uint rank = min(group / slab_groups, maximum_effects - 1);
    uint slot = min(frame.effect_order[rank / 4][rank % 4], maximum_effects - 1);
    uint layer_index = group_layer(classes, frame.effect_states[slot].w, group % slab_groups);
    return layer_index == layer_debris ? category_debris : smoke_like(layer_index) ? category_smoke : category_light;
}

uint3 category_vector(uint category, uint count)
{
    return uint3(category == category_smoke ? count : 0, category == category_light ? count : 0, category == category_debris ? count : 0);
}

[numthreads(scan_threads, 1, 1)]
void cs_main(uint thread : SV_GroupIndex)
{
    uint group_count = min(parameters.group_count, maximum_groups);
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
    for (uint group = first; group < last; ++group)
    {
        total += category_vector(group_values[group] >> category_shift, group_values[group] & count_mask);
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
    for (uint group = first; group < last; ++group)
    {
        uint category = group_values[group] >> category_shift;
        uint count = group_values[group] & count_mask;
        uint translucent = parameters.layered != 0 ? (category == category_smoke ? before.x : before.y) : before.x + before.y;
        group_values[group] = category == category_debris ? before.z : translucent;
        before += category_vector(category, count);
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint stored = thread; stored < group_count; stored += scan_threads)
    {
        offsets.Store(stored * 4, group_values[stored]);
    }
    if (thread == scan_threads - 1)
    {
        uint3 alive = sums[thread];
        arguments.Store4(particle_arguments_offset, uint4(particle_vertices, alive.x + alive.y, 0, 0));
        arguments.Store4(smoke_arguments_offset, uint4(particle_vertices, alive.x, 0, 0));
        arguments.Store4(light_arguments_offset, uint4(particle_vertices, alive.y, 0, 0));
        arguments.Store4(debris_arguments_offset, uint4(debris_vertices, alive.z, 0, 0));
        arguments.Store4(counters_offset, uint4(parameters.tick, alive.x + alive.y + alive.z, alive.x, alive.y));
        arguments.Store4(counters_offset + 16, uint4(alive.z, 0, 0, 0));
    }
}
