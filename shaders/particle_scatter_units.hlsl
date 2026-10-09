#define PYROCLAST_REALISM
#include "particle_realism.hlsli"

struct Parameters
{
    uint unit_count;
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

struct GroupRecords
{
    uint first_record;
    uint slot;
    bool residual;
    uint layer_index;
    uint local;
};

GroupRecords group_records(uint group, uint thread)
{
    Unit unit = find_unit(group);
    uint group_in_unit = group - unit.first_group;
    GroupRecords records;
    records.residual = unit_is_cluster(unit);
    if (records.residual)
    {
        Cluster cluster = load_cluster(unit.id - unit_cluster_first);
        records.first_record = residual_first + (cluster.ring_group + group_in_unit) * simulation_group_size;
        records.slot = maximum_effects;
        records.layer_index = layer_blast_smoke;
        records.local = group_in_unit * simulation_group_size + thread;
    }
    else
    {
        records.slot = min(unit.id, maximum_effects - 1);
        records.first_record = records.slot * explosion_particles + group_in_unit * simulation_group_size;
        records.layer_index = group_layer(classes, frame.effect_states[records.slot].w, group_in_unit);
        records.local = group_in_unit * simulation_group_size + thread;
    }
    return records;
}

uint list_first(GroupRecords records)
{
    return records.layer_index == layer_debris ? debris_list_first :
        parameters.layered != 0 && !smoke_like(records.layer_index) ? light_list_first : 0;
}

[numthreads(simulation_group_size, 1, 1)]
void cs_main(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    if (group.x >= min(frame.unit_state.y, maximum_unit_groups)) return;
    uint2 bits = alive_masks.Load2(group.x * 8);
    uint bit = 1u << (thread.x % 32);
    bool low = thread.x < 32;
    if (((low ? bits.x : bits.y) & bit) == 0) return;
    uint place = low ? countbits(bits.x & (bit - 1)) : countbits(bits.x) + countbits(bits.y & (bit - 1));
    GroupRecords records = group_records(group.x, thread.x);
    uint index = list_first(records) + offsets.Load(group.x * 4) + place;
    if (index < debris_list_first + maximum_effects * explosion_particles)
    {
        alive.Store(index * 4, records.first_record + thread.x);
    }
}

static const uint measured_words = 8;
groupshared uint measured[measured_words];

[numthreads(simulation_group_size, 1, 1)]
void cs_measure(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    if (group.x >= min(frame.unit_state.y, maximum_unit_groups)) return;
    if (thread.x < measured_words) measured[thread.x] = 0;
    uint2 bits = alive_masks.Load2(group.x * 8);
    uint bit = 1u << (thread.x % 32);
    bool low = thread.x < 32;
    bool listed = ((low ? bits.x : bits.y) & bit) != 0;
    GroupRecords records = group_records(group.x, thread.x);
    uint record = records.first_record + thread.x;
    if (listed)
    {
        uint place = low ? countbits(bits.x & (bit - 1)) : countbits(bits.x) + countbits(bits.y & (bit - 1));
        uint index = list_first(records) + offsets.Load(group.x * 4) + place;
        if (index < debris_list_first + maximum_effects * explosion_particles) alive.Store(index * 4, record);
    }
    GroupMemoryBarrierWithGroupSync();
    if (listed && records.layer_index != layer_debris && record < min(parameters.record_count, pool_records))
    {
        bool smoke = smoke_like(records.layer_index);
        RealismFootprint realism = realism_footprint(particles, classes, record);
        Footprint footprint;
        bool odd;
        if (realism.realism)
        {
            footprint = realism.footprint;
            odd = realism.survivor ? (realism.survivor_index & 1) != 0 && realism.crossfade >= 1 : (records.local & 1) != 0;
        }
        else
        {
            uint effect_class = frame.effect_states[records.slot].w;
            footprint = particle_footprint(particles, record, load_layer(classes, effect_class, records.layer_index), smoke);
            odd = (records.local & 1) != 0;
        }
        if (smoke && parameters.lod_pixels > 0 && odd && footprint.pixels >= 2 * parameters.lod_pixels) InterlockedAdd(measured[6], 1);
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
