#include "frame_constants.hlsli"

struct Parameters
{
    uint effect_count;
    uint layered;
};
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<Parameters> parameters : register(b0);
ByteAddressBuffer alive_masks : register(t0);
ByteAddressBuffer offsets : register(t1);
RWByteAddressBuffer alive : register(u0);

[numthreads(simulation_group_size, 1, 1)]
void cs_main(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    uint rank = group.x / slab_groups;
    if (rank >= min(parameters.effect_count, maximum_effects)) return;
    uint2 bits = alive_masks.Load2(group.x * 8);
    uint bit = 1u << (thread.x % 32);
    bool low = thread.x < 32;
    if (((low ? bits.x : bits.y) & bit) == 0) return;
    uint place = low ? countbits(bits.x & (bit - 1)) : countbits(bits.x) + countbits(bits.y & (bit - 1));
    uint slot = frame.effect_order[rank / 4][rank % 4];
    uint local = (group.x % slab_groups) * simulation_group_size + thread.x;
    bool light = local >= smoke_particles;
    uint index = (parameters.layered != 0 && light ? light_list_first : 0) + offsets.Load(group.x * 4) + place;
    if (slot < maximum_effects && index < maximum_effects * explosion_particles)
    {
        alive.Store(index * 4, slot * explosion_particles + local);
    }
}
