#include "realism_constants.hpp"
#include "../simulation/simulation_clock.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <vector>

namespace pyroclast
{
namespace layout = realism_layout;

namespace
{
// `id`: a slot, or unit_cluster_first + a cluster slot.
struct UnitKey
{
    std::uint32_t id;
    std::uint32_t groups;
    float depth;
    std::uint64_t serial;
    bool cluster;
    bool first = false;
};
}

float pall_shadow_power(const ExplosionSystem& effects, std::uint32_t tick)
{
    const auto ease = [](float x) {
        x = std::clamp(x, 0.0f, 1.0f);
        return x * x * (3 - 2 * x);
    };
    float pall = 0;
    for (const auto& cluster : effects.residual().clusters())
    {
        if (tick < cluster.graduation_tick || tick >= cluster.visible_end()) continue;
        float weight = ease(static_cast<float>(tick - cluster.graduation_tick) / static_cast<float>(pall_ease_in_ticks)) *
            ease(static_cast<float>(cluster.natural_end - tick) / static_cast<float>(pall_ease_out_ticks));
        if (cluster.fade_end != layout::no_fade)
        {
            weight *= ease(static_cast<float>(cluster.fade_end - tick) / static_cast<float>((cluster.fade_code + 1) * layout::fade_unit_ticks));
        }
        pall += weight;
    }
    return 1 + std::min(pall / 64.0f, 2.0f);
}

FrameUnits count_units(const ExplosionSystem& effects, std::uint32_t tick)
{
    FrameUnits units{static_cast<std::uint32_t>(effects.active().size()), static_cast<std::uint32_t>(effects.active().size()) * particle_layout::slab_groups};
    if (effects.rotor_disc_slot()) units.groups -= particle_layout::slab_groups - 1;
    for (const auto& cluster : effects.residual().clusters())
    {
        if (!cluster.listed(tick)) continue;
        ++units.units;
        units.groups += cluster.groups;
    }
    return units;
}

RealismUnits fill_realism_constants(FrameConstants& constants, const ExplosionSystem& effects, std::uint32_t tick, std::uint32_t key_tick,
    const Camera& camera, EffectOrder order, float3 drift, const LingerSettings& linger, std::optional<float3> jet_key, std::optional<float3> rotor_key,
    std::optional<float3> disc_key)
{
    const auto forward = camera.forward();
    const auto depth = [&camera, forward, key_tick, order, drift](float3 origin, float3 drift_start, ExplosionPreset preset, std::uint32_t start) {
        auto centre = origin + (drift - drift_start);
        if (order == EffectOrder::cloud)
        {
            const float seconds = static_cast<float>(key_tick - std::min(key_tick, start)) / ticks_per_second;
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
        if (slot.kind == EffectKind::jet)
        {
            const float3 point = jet_key.value_or(slot.origin);
            keys.push_back({index, particle_layout::slab_groups, dot(point - camera.position, forward), slot.serial, false});
            constants.effect_residual[index] = {2u, realism_layout::no_fade, layout::latched_rotor, slot.start_tick + slot.lifetime_ticks};
            continue;
        }
        if (slot.kind != EffectKind::explosion)
        {
            const bool discs = slot.kind == EffectKind::rotor_disc;
            const float3 point = (discs ? disc_key : rotor_key).value_or(slot.origin);
            assert(std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z));
            keys.push_back({index, discs ? 1u : particle_layout::slab_groups, dot(point - camera.position, forward), slot.serial, false, discs});
            const std::uint32_t end = slot.start_tick + slot.lifetime_ticks;
            constants.effect_residual[index] = {discs ? 1u : 0u, realism_layout::no_fade, layout::latched_rotor, end};
            continue;
        }
        keys.push_back({index, particle_layout::slab_groups, depth(slot.origin, slot.drift_start, slot.preset, slot.start_tick), slot.serial, false});
        if (const auto& link = slot.residual)
        {
            const auto carried = drift - slot.drift_start;
            constants.effect_residual[index] = {link->ring_group | (link->cluster << layout::effect_cluster_shift) | (link->tag << layout::effect_tag_shift),
                link->graduation_tick, link->longest_ticks | slot.latched, half_bits(carried.x) | (half_bits(carried.z) << 16)};
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
        if (a.first != b.first) return a.first;
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
    constants.residual = {static_cast<float>(layout::crossfade_ticks), static_cast<float>(linger.linger_ticks), pall_shadow_power(effects, key_tick), 0};
    return {{units, groups}, listed};
}
}
