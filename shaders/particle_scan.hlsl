#include "particle_layout.hlsli"

struct Parameters
{
    uint group_count;
    uint tick;
};
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<Parameters> parameters : register(b0);
ByteAddressBuffer alive_masks : register(t0);
RWByteAddressBuffer offsets : register(u0);
RWByteAddressBuffer arguments : register(u1);

groupshared uint2 sums[scan_threads];

uint alive_count(uint group)
{
    uint2 bits = alive_masks.Load2(group * 8);
    return countbits(bits.x) + countbits(bits.y);
}

bool smoke_group(uint group)
{
    return group % slab_groups < smoke_groups;
}

[numthreads(scan_threads, 1, 1)]
void cs_main(uint thread : SV_GroupIndex)
{
    uint group_count = min(parameters.group_count, maximum_effects * slab_groups);
    uint run = (group_count + scan_threads - 1) / scan_threads;
    uint first = min(thread * run, group_count);
    uint last = min(first + run, group_count);
    uint2 total = uint2(0, 0);
    for (uint group = first; group < last; ++group)
    {
        uint count = alive_count(group);
        if (smoke_group(group)) total.x += count;
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
        uint count = alive_count(group);
        bool smoke = smoke_group(group);
        offsets.Store2(group * 8, uint2(before.x + before.y, smoke ? before.x : before.y));
        if (smoke) before.x += count;
        else before.y += count;
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
