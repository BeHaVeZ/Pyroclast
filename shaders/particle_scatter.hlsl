#include "effect_classes.hlsli"
#include "frame_constants.hlsli"
#include "particle_footprint.hlsli"

struct Parameters
{
    uint effect_count;
    uint layered;
    uint record_count;
    float lod_pixels;
    float faint_floor;
};
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<Parameters> parameters : register(b0);
ByteAddressBuffer alive_masks : register(t0);
ByteAddressBuffer offsets : register(t1);
ByteAddressBuffer classes : register(t2);
RWByteAddressBuffer alive : register(u0);
ByteAddressBuffer particles : register(t3);
RWByteAddressBuffer arguments : register(u1);

// `layered` must match the scan's. Root and push descriptors carry no size: never write past the lists.
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
    uint layer_index = group_layer(classes, frame.effect_states[min(slot, maximum_effects - 1)].w, group.x % slab_groups);
    uint first = layer_index == layer_debris ? debris_list_first : parameters.layered != 0 && !smoke_like(layer_index) ? light_list_first : 0;
    uint index = first + offsets.Load(group.x * 4) + place;
    if (slot < maximum_effects && index < debris_list_first + maximum_effects * explosion_particles)
    {
        alive.Store(index * 4, slot * explosion_particles + local);
    }
}

static const uint measured_words = 8;
groupshared uint measured[measured_words];

// Adds to the counter words the scan zeroed; a word saturates rather than wraps.
[numthreads(simulation_group_size, 1, 1)]
void cs_measure(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    uint rank = group.x / slab_groups;
    if (rank >= min(parameters.effect_count, maximum_effects)) return;
    if (thread.x < measured_words) measured[thread.x] = 0;
    uint2 bits = alive_masks.Load2(group.x * 8);
    uint bit = 1u << (thread.x % 32);
    bool low = thread.x < 32;
    bool listed = ((low ? bits.x : bits.y) & bit) != 0;
    uint slot = frame.effect_order[rank / 4][rank % 4];
    uint local = (group.x % slab_groups) * simulation_group_size + thread.x;
    uint effect_class = frame.effect_states[min(slot, maximum_effects - 1)].w;
    uint layer_index = group_layer(classes, effect_class, group.x % slab_groups);
    if (listed)
    {
        uint place = low ? countbits(bits.x & (bit - 1)) : countbits(bits.x) + countbits(bits.y & (bit - 1));
        uint first = layer_index == layer_debris ? debris_list_first : parameters.layered != 0 && !smoke_like(layer_index) ? light_list_first : 0;
        uint index = first + offsets.Load(group.x * 4) + place;
        if (slot < maximum_effects && index < debris_list_first + maximum_effects * explosion_particles)
        {
            alive.Store(index * 4, slot * explosion_particles + local);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    uint record = slot * explosion_particles + local;
    if (listed && layer_index != layer_debris && slot < maximum_effects && record < parameters.record_count)
    {
        bool smoke = smoke_like(layer_index);
        Footprint footprint = particle_footprint(particles, record, load_layer(classes, effect_class, layer_index), smoke);
        if (smoke && parameters.lod_pixels > 0 && (local & 1) != 0 && footprint.pixels >= 2 * parameters.lod_pixels)
        {
            InterlockedAdd(measured[6], 1);
        }
        if (smoke && parameters.faint_floor > 0 && footprint.opacity < parameters.faint_floor) InterlockedAdd(measured[7], 1);
        if (sphere_outside_view(footprint.position, footprint.bound))
        {
            InterlockedAdd(measured[5], 1);
        }
        else
        {
            uint area = uint(screen_area(footprint) + 0.5);
            uint bin = !smoke ? 4 : footprint.pixels < coverage_bin_small ? 0 : footprint.pixels < coverage_bin_medium ? 1 :
                footprint.pixels < coverage_bin_large ? 2 : 3;
            if (area != 0) InterlockedAdd(measured[bin], area);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (thread.x < measured_words)
    {
        uint value = measured[thread.x];
        uint word = thread.x == 5 ? counter_offscreen_word : thread.x == 6 ? counter_thinned_word : counter_faint_word;
        if (thread.x < 5)
        {
            value = (value + coverage_unit_pixels / 2) / coverage_unit_pixels;
            word = thread.x < 4 ? counter_smoke_coverage_word + thread.x : counter_light_coverage_word;
        }
        if (value != 0)
        {
            uint before;
            arguments.InterlockedAdd(counters_offset + word * 4, value, before);
            if (before + value < before) arguments.InterlockedMax(counters_offset + word * 4, 0xffffffffu);
        }
    }
}
