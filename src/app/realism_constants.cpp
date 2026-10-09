#include "realism_constants.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <vector>

namespace pyroclast
{
namespace layout = realism_layout;

namespace
{
std::uint32_t to_half(float value)
{
    const auto bits = std::bit_cast<std::uint32_t>(value);
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const int exponent = static_cast<int>((bits >> 23) & 0xffu) - 127 + 15;
    if (exponent <= 0) return sign;
    if (exponent >= 31) return sign | 0x7bffu;
    const std::uint32_t mantissa = bits & 0x7fffffu;
    std::uint32_t half = sign | (static_cast<std::uint32_t>(exponent) << 10) | (mantissa >> 13);
    const std::uint32_t rest = mantissa & 0x1fffu;
    if (rest > 0x1000u || (rest == 0x1000u && (half & 1u) != 0)) ++half;
    return half;
}

struct UnitKey
{
    std::uint32_t id;
    std::uint32_t groups;
    float depth;
    std::uint64_t serial;
    bool cluster;
};
}

FrameUnits count_units(const ExplosionSystem& effects, std::uint32_t tick)
{
    FrameUnits units{static_cast<std::uint32_t>(effects.active().size()), static_cast<std::uint32_t>(effects.active().size()) * particle_layout::slab_groups};
    for (const auto& cluster : effects.residual().clusters())
    {
        if (!cluster.listed(tick)) continue;
        ++units.units;
        units.groups += cluster.groups;
    }
    return units;
}

RealismUnits fill_realism_constants(FrameConstants& constants, const ExplosionSystem& effects, std::uint32_t tick, std::uint32_t key_tick,
    const Camera& camera, EffectOrder order, float3 drift, const LingerSettings& linger)
{
    const auto forward = camera.forward();
    const auto depth = [&camera, forward, key_tick, order, drift](float3 origin, float3 drift_start, ExplosionPreset preset, std::uint32_t start) {
        auto centre = origin + (drift - drift_start);
        if (order == EffectOrder::cloud)
        {
            const float seconds = static_cast<float>(key_tick - std::min(key_tick, start)) / 60.0f;
            centre.y += cloud_rise(preset, seconds);
        }
        return dot(centre - camera.position, forward);
    };
    std::vector<UnitKey> keys;
    keys.reserve(layout::maximum_units);
    const auto slots = effects.slots();
    constants.effect_residual = {};
    for (const auto index : effects.active())
    {
        const auto& slot = slots[index];
        keys.push_back({index, particle_layout::slab_groups, depth(slot.origin, slot.drift_start, slot.preset, slot.start_tick), slot.serial, false});
        if (const auto& link = slot.residual)
        {
            const auto carried = drift - slot.drift_start;
            constants.effect_residual[index] = {link->ring_group | (link->cluster << layout::effect_cluster_shift) | (link->tag << layout::effect_tag_shift),
                link->graduation_tick, link->longest_ticks | slot.latched, to_half(carried.x) | (to_half(carried.z) << 16)};
        }
    }
    constants.clusters = {};
    std::uint32_t listed = 0;
    for (const auto& cluster : effects.residual().clusters())
    {
        if (!cluster.listed(tick)) continue;
        keys.push_back({layout::unit_cluster_first + cluster.slot, cluster.groups,
            depth(cluster.origin, cluster.drift_start, cluster.preset, cluster.start_tick), cluster.serial, true});
        auto& pair = constants.clusters[cluster.slot / 2];
        const std::uint32_t half = (cluster.slot & 1) * 2;
        pair[half] = cluster.ring_group | (cluster.records << layout::record_count_shift) | (cluster.fade_code << layout::fade_code_shift) |
            (cluster.tag << layout::tag_shift);
        pair[half + 1] = cluster.fade_end;
        ++listed;
    }
    assert(keys.size() <= layout::maximum_units);
    std::stable_sort(keys.begin(), keys.end(), [order](const UnitKey& a, const UnitKey& b) {
        if (order != EffectOrder::none && a.depth != b.depth) return a.depth > b.depth;
        if (a.serial != b.serial) return a.serial < b.serial;
        return !a.cluster && b.cluster;
    });
    constants.unit_order = {};
    std::uint32_t groups = 0;
    for (std::size_t index = 0; index < keys.size(); ++index)
    {
        constants.unit_order[index / 4][index % 4] = keys[index].id | (groups << layout::unit_id_bits);
        groups += keys[index].groups;
    }
    assert(groups <= layout::maximum_unit_groups);
    const auto units = static_cast<std::uint32_t>(keys.size());
    constants.unit_state = {units, groups, listed, 0};
    constants.residual = {static_cast<float>(layout::crossfade_ticks), static_cast<float>(linger.linger_ticks), 0, 0};
    return {{units, groups}, listed};
}
}
