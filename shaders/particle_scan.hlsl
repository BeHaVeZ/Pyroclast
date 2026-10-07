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
groupshared uint2 sums[scan_threads];

static const uint smoke_flag = 0x80000000u;

bool smoke_group(uint group)
{
    uint rank = min(group / slab_groups, maximum_effects - 1);
    uint slot = min(frame.effect_order[rank / 4][rank % 4], maximum_effects - 1);
    return smoke_like(group_layer(classes, frame.effect_states[slot].w, group % slab_groups));
}

[numthreads(scan_threads, 1, 1)]
void cs_main(uint thread : SV_GroupIndex)
{
    uint group_count = min(parameters.group_count, maximum_groups);
    for (uint loaded = thread; loaded < group_count; loaded += scan_threads)
    {
        uint2 bits = alive_masks.Load2(loaded * 8);
        group_values[loaded] = (countbits(bits.x) + countbits(bits.y)) | (smoke_group(loaded) ? smoke_flag : 0);
    }
    GroupMemoryBarrierWithGroupSync();
    uint run = (group_count + scan_threads - 1) / scan_threads;
    uint first = min(thread * run, group_count);
    uint last = min(first + run, group_count);
    uint2 total = uint2(0, 0);
    for (uint group = first; group < last; ++group)
    {
        uint count = group_values[group] & ~smoke_flag;
        if (group_values[group] & smoke_flag) total.x += count;
        else total.y += count;
    }
    sums[thread] = total;
    GroupMemoryBarrierWithGroupSync();
    for (uint span = 1; span < scan_threads; span *= 2)
    {
        uint2 earlier = thread >= span ? sums[thread - span] : uint2(0, 0);
        GroupMemoryBarrierWithGroupSync();
        sums[thread] += earlier;
        GroupMemoryBarrierWithGroupSync();
    }
    uint2 before = sums[thread] - total;
    for (uint group = first; group < last; ++group)
    {
        uint count = group_values[group] & ~smoke_flag;
        bool smoke = (group_values[group] & smoke_flag) != 0;
        group_values[group] = parameters.layered != 0 ? (smoke ? before.x : before.y) : before.x + before.y;
        if (smoke) before.x += count;
        else before.y += count;
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint stored = thread; stored < group_count; stored += scan_threads)
    {
        offsets.Store(stored * 4, group_values[stored]);
    }
    if (thread == scan_threads - 1)
    {
        uint2 alive = sums[thread];
        arguments.Store4(particle_arguments_offset, uint4(particle_vertices, alive.x + alive.y, 0, 0));
        arguments.Store4(smoke_arguments_offset, uint4(particle_vertices, alive.x, 0, 0));
        arguments.Store4(light_arguments_offset, uint4(particle_vertices, alive.y, 0, 0));
        arguments.Store4(counters_offset, uint4(parameters.tick, alive.x + alive.y, alive.x, alive.y));
    }
}
