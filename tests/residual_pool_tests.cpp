#include "simulation/explosion_system.hpp"
#include "simulation/residual_pool.hpp"
#include "simulation/wind.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
int failures = 0;

void check(bool value, const char* name)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

struct Random
{
    std::uint32_t state;
    std::uint32_t next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
};

std::uint32_t layout_layers()
{
    return pyroclast::particle_layout::layer_count;
}

float smoothstep(float x)
{
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3 - 2 * x);
}

float fade_factor(const pyroclast::ResidualCluster& cluster, std::uint32_t tick)
{
    namespace layout = pyroclast::realism_layout;
    if (cluster.fade_end == layout::no_fade) return 1;
    if (tick >= cluster.fade_end) return 0;
    return smoothstep(static_cast<float>(cluster.fade_end - tick) / static_cast<float>((cluster.fade_code + 1) * layout::fade_unit_ticks));
}

struct Stream
{
    double rate;
    std::vector<pyroclast::ExplosionPreset> presets;
    std::uint32_t ticks;
    std::uint32_t seed;
    std::uint32_t linger_seconds = 75;
    std::uint32_t split_seed = 0;
    bool varied = true;
};

using Schedule = std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>;

struct Outcome
{
    std::uint64_t forced = 0;
    std::uint32_t peak_listed = 0;
    std::uint64_t started = 0;
    std::uint64_t submitted = 0;
    std::uint64_t dropped = 0;
    std::uint64_t deferred = 0;
    std::size_t waiting = 0;
    bool early_clean = true;
    bool overwrite_safe = true;
    bool slot_safe = true;
    bool fades_smooth = true;
    bool fades_long = true;
    bool bounded = true;
    std::uint32_t lowest_scale = pyroclast::realism_layout::scale_one;
    std::uint64_t refused = 0;
    std::vector<std::vector<std::uint64_t>> listings;
    std::map<std::uint64_t, Schedule> schedules;
};

Outcome run(const Stream& stream, pyroclast::SlotPolicy policy = pyroclast::SlotPolicy::drop_new, bool frames_only = false)
{
    using namespace pyroclast;
    namespace layout = realism_layout;
    ExplosionSystem system(particle_layout::maximum_effects);
    Random random{stream.seed};
    const auto mask = stream.varied ? layout::realism_full_mask : layout::realism_full_mask & ~(1u << layout::variation_mask_bit);
    RealismStart realism{mask, {stream.linger_seconds * 60, false}, {}};
    Outcome outcome;
    std::map<std::uint64_t, ResidualCluster> seen;
    std::map<std::uint64_t, float> previous_factor;
    std::map<std::uint64_t, std::pair<std::uint32_t, std::uint32_t>> previous_fade;
    std::vector<bool> frame_first(stream.ticks, true);
    if (stream.split_seed != 0)
    {
        Random split{stream.split_seed};
        for (std::uint32_t tick = 0; tick < stream.ticks;)
        {
            const std::uint32_t length = 1 + split.next() % 4;
            for (std::uint32_t inside = 1; inside < length && tick + inside < stream.ticks; ++inside) frame_first[tick + inside] = false;
            tick += length;
        }
    }
    double carry = 0;
    for (std::uint32_t tick = 0; tick < stream.ticks; ++tick)
    {
        carry += stream.rate;
        while (frame_first[tick] && carry >= 1)
        {
            carry -= 1;
            const auto preset = stream.presets[random.next() % stream.presets.size()];
            system.submit({{static_cast<float>(random.next() % 60) - 30, 0, static_cast<float>(random.next() % 60) - 30}, {0, 1, 0},
                random.next(), preset});
            ++outcome.submitted;
        }
        const auto before = system.residual().forced_fades();
        if (!frames_only || frame_first[tick]) system.start(tick, policy, &realism);
        for (const auto index : system.active())
        {
            const auto& slot = system.slots()[index];
            if (!slot.graduating_early) continue;
            const auto lifetime = slot.lifetime_ticks;
            const auto effect = make_effect_class(slot.preset, SurfaceType::dirt);
            for (std::uint32_t layer = 3; layer < layout_layers(); ++layer)
            {
                const auto& source = effect.layers[layer];
                if (source.count != 0) outcome.early_clean = outcome.early_clean && lifetime * 1.0f >= (source.delay_range + source.lifetime_min + source.lifetime_range) * 60 - 1;
            }
        }
        for (const auto& cluster : system.residual().clusters())
        {
            const std::pair<std::uint32_t, std::uint32_t> fade{cluster.fade_end, cluster.fade_code};
            const auto last = previous_fade.find(cluster.serial);
            const std::pair<std::uint32_t, std::uint32_t> old = last != previous_fade.end() ? last->second :
                std::pair<std::uint32_t, std::uint32_t>{layout::no_fade, 0};
            previous_fade[cluster.serial] = fade;
            if (fade == old || cluster.fade_end == layout::no_fade) continue;
            const auto length = (cluster.fade_code + 1) * layout::fade_unit_ticks;
            const auto old_length = (old.second + 1) * layout::fade_unit_ticks;
            const bool under_way = old.first != layout::no_fade && cluster.graduation_tick <= tick && tick + old_length > old.first;
            if (under_way)
            {
                auto before_change = cluster;
                before_change.fade_end = old.first;
                before_change.fade_code = old.second;
                outcome.fades_long = outcome.fades_long && fade_factor(cluster, tick) <= fade_factor(before_change, tick) + 1e-6f;
            }
            else
            {
                const auto from = std::max(tick, cluster.graduation_tick);
                outcome.fades_long = outcome.fades_long && cluster.fade_end >= from + layout::fade_unit_ticks && cluster.fade_end - length >= from;
            }
        }
        if (system.residual().forced_fades() != before) outcome.forced += system.residual().forced_fades() - before;
        outcome.lowest_scale = std::min(outcome.lowest_scale, system.residual().last_scale());
        std::vector<std::uint64_t> listed;
        for (const auto& cluster : system.residual().clusters())
        {
            seen[cluster.serial] = cluster;
            outcome.bounded = outcome.bounded && cluster.ring_group + cluster.groups <= layout::residual_groups &&
                cluster.slot < layout::maximum_clusters && cluster.records <= (1u << layout::record_count_bits) - 1;
            if (!cluster.listed(tick)) continue;
            listed.push_back(cluster.serial);
            const float factor = fade_factor(cluster, tick);
            const auto previous = previous_factor.find(cluster.serial);
            if (previous != previous_factor.end())
            {
                outcome.fades_smooth = outcome.fades_smooth && factor <= previous->second + 1e-6f && previous->second - factor <= 1.5f / 120 + 1e-4f;
            }
            else outcome.fades_smooth = outcome.fades_smooth && factor == 1;
            previous_factor[cluster.serial] = factor;
        }
        outcome.peak_listed = std::max(outcome.peak_listed, static_cast<std::uint32_t>(listed.size()));
        outcome.listings.push_back(std::move(listed));
    }
    outcome.started = system.started();
    outcome.refused = system.residual().refused();
    for (const auto& [serial, cluster] : seen)
    {
        outcome.schedules[serial] = {cluster.ring_group, cluster.slot, cluster.graduation_tick, cluster.natural_end, cluster.fade_end, cluster.fade_code};
    }
    outcome.dropped = system.dropped();
    outcome.deferred = system.deferred();
    outcome.waiting = system.waiting();
    std::vector<ResidualCluster> all;
    for (const auto& [serial, cluster] : seen) all.push_back(cluster);
    for (std::size_t a = 0; a < all.size(); ++a)
    {
        for (std::size_t b = a + 1; b < all.size(); ++b)
        {
            const auto& older = all[a].graduation_tick <= all[b].graduation_tick ? all[a] : all[b];
            const auto& newer = &older == &all[a] ? all[b] : all[a];
            const bool overlaps = older.ring_group < newer.ring_group + newer.groups && newer.ring_group < older.ring_group + older.groups;
            if (overlaps) outcome.overwrite_safe = outcome.overwrite_safe && older.visible_end() + 4 <= newer.graduation_tick;
            if (older.slot == newer.slot) outcome.slot_safe = outcome.slot_safe && older.visible_end() + 4 <= newer.graduation_tick;
        }
    }
    return outcome;
}
}

int main()
{
    using namespace pyroclast;
    namespace layout = realism_layout;

    {
        bool spread = true;
        std::uint32_t lowest = 2000;
        std::uint32_t highest = 0;
        for (std::uint32_t seed = 0; seed < 4096; ++seed)
        {
            const auto variation = event_variation(seed);
            spread = spread && variation.key < 256 && variation.life >= 888 && variation.life <= 1229;
            lowest = std::min(lowest, variation.life);
            highest = std::max(highest, variation.life);
        }
        check(spread && lowest == 888 && highest == 1229, "every seed's variation is in range, and the range is used");
        check(particle_hash(0) == 0 && particle_hash(1) == 0x688990c0u && particle_hash(12345) == 0x912efcf7u && event_variation(7).key == (particle_hash(7u ^ 0xa5a5a5a5u) & 0xffu) &&
            event_variation(7).life == 888 + particle_hash(7u ^ 0x3c6ef372u) % 342, "the variation hash is lowbias32");
    }

    check(ResidualPool::death_tick(100, 580, 4500, 1024, 0) == 4600, "an artillery-he record lives 75 s from its start");
    check(ResidualPool::death_tick(100, 580, 4500, 1024, 300) == 4300, "jitter takes up to 300 ticks off");
    check(ResidualPool::death_tick(100, 580, 4500, 1229, 0) == 100 + ((4500 * 1229) >> 10), "variation lengthens the life");
    check(ResidualPool::death_tick(100, 700, 600, 1024, 300) == 700 + layout::crossfade_ticks + 60, "a record outlives graduation by 120 ticks");
    check(ResidualPool::death_tick(0, 480, 600, 1024, 0) == 480 + layout::crossfade_ticks + 60, "a short linger still crossfades");

    {
        WindDrift drift;
        const WindSettings wind{3, 30, 0.4f};
        const auto ahead = drift.at(240, wind, true);
        drift.advance(240, wind, true);
        const auto committed = drift.at(240, wind, true);
        check(ahead.x == committed.x && ahead.z == committed.z && drift.tick() == 240, "the drift ahead equals the committed drift");
        WindDrift steady;
        steady.advance(600, {3, 0, 0}, false);
        check(std::abs(steady.at(600, {3, 0, 0}, false).x - 30) < 1e-3f, "10 s of a 3 m/s wind carries the air 30 m");
    }

    {
        ExplosionSystem system(4);
        const RealismStart realism{layout::realism_full_mask & ~(1u << layout::variation_mask_bit), {4500, false}, {}};
        system.submit({{0, 0, 0}, {0, 1, 0}, 7, ExplosionPreset::artillery_he});
        system.start(100, SlotPolicy::drop_new, &realism);
        const auto& slot = system.slots()[0];
        const auto* cluster = slot.residual ? system.residual().find(*slot.residual) : nullptr;
        check(slot.latched == layout::latched_linger && slot.residual && slot.lifetime_ticks == realism_effect_ticks(ExplosionPreset::artillery_he, SurfaceType::dirt) &&
            cluster && cluster->graduation_tick == 640 && cluster->records == 176 && cluster->groups == 3 && cluster->ring_group == 0 &&
            cluster->natural_end == 4600, "a lingering artillery-he effect reserves 3 groups; its survivors move after its 480 ticks and the crossfade");
        check(cluster && !cluster->listed(639) && cluster->listed(640) && cluster->listed(4599) && !cluster->listed(4600),
            "its cluster is listed from its graduation to its last record's death");
        const RealismStart without{layout::realism_full_mask & ~(1u << layout::linger_mask_bit), {4500, false}, {}};
        system.submit({{0, 0, 0}, {0, 1, 0}, 8, ExplosionPreset::artillery_he});
        system.start(101, SlotPolicy::drop_new, &without);
        check(!system.slots()[1].residual && system.slots()[1].latched == 0 &&
            system.slots()[1].lifetime_ticks == effect_ticks(ExplosionPreset::artillery_he, SurfaceType::dirt),
            "an effect started without the linger bit keeps its legacy life");
        system.submit({{0, 0, 0}, {0, 1, 0}, 9, ExplosionPreset::artillery_he});
        const RealismStart varying{layout::realism_full_mask, {4500, true}, {}};
        system.start(102, SlotPolicy::drop_new, &varying);
        const auto& varied = system.slots()[2];
        const auto* varied_cluster = varied.residual ? system.residual().find(*varied.residual) : nullptr;
        const auto life = event_variation(9).life;
        check(varied.latched == (layout::latched_linger | layout::latched_variation) && varied_cluster && life >= 888 && life <= 1229 &&
            varied_cluster->natural_end == ResidualPool::death_tick(102, varied_cluster->graduation_tick, varied.residual->longest_ticks, life, 0),
            "a varied effect's cluster ends where its own life factor puts its records");
        system.clear();
        check(system.residual().clusters().empty() && !system.slots()[0].residual, "clear forgets every cluster");
    }

    const Stream barrage{4.0 / 60, {ExplosionPreset::artillery_he}, 60 * 120, 11};
    const Stream sustained{12.0 / 60, {ExplosionPreset::artillery_he, ExplosionPreset::small_he}, 60 * 150, 23};
    const Stream saturation{30.0 / 60, {ExplosionPreset::heavy, ExplosionPreset::small_impact, ExplosionPreset::small_impact,
        ExplosionPreset::small_impact}, 60 * 60, 37};
    const Stream short_linger{20.0 / 60, {ExplosionPreset::small_impact, ExplosionPreset::dust_blast}, 60 * 60, 41, 10};
    for (const auto* stream : {&barrage, &sustained, &saturation, &short_linger})
    {
        const auto outcome = run(*stream);
        std::printf("stream %.0f/s: %llu started, peak %u clusters listed, %llu forced fades, lowest linger scale %u/1024\n", stream->rate * 60,
            static_cast<unsigned long long>(outcome.started), outcome.peak_listed, static_cast<unsigned long long>(outcome.forced), outcome.lowest_scale);
        check(outcome.overwrite_safe, "no ring range is written while an older cluster on it can still be listed");
        check(outcome.slot_safe, "no cluster slot is reused while its older cluster can still be listed");
        check(outcome.fades_smooth, "forced fades never brighten a cluster and never step faster than a 120-tick fade");
        check(outcome.fades_long, "every forced fade lasts at least 120 ticks from now or the cluster's graduation, or continues one under way");
        check(outcome.refused == 0, "every lingering effect finds a ring range");
        check(outcome.bounded && outcome.peak_listed <= layout::maximum_clusters, "clusters stay within the ring, the slots and 9-bit counts");
        check(run(*stream).listings == outcome.listings, "a stream's listing is deterministic");
    }
    const auto calm = run(barrage);
    check(calm.forced == 0 && calm.lowest_scale == 970, "4 artillery-he impacts a second fit the ring without forced fades, at a linger scale of 970/1024");
    auto plain_barrage = barrage;
    plain_barrage.varied = false;
    const auto plain = run(plain_barrage);
    check(plain.forced == 0 && plain.lowest_scale == layout::scale_one, "without variation they keep the full linger");
    const auto busy = run(sustained);
    check(busy.lowest_scale < layout::scale_one, "12 impacts a second shorten the linger");
    const auto saturated = run(saturation);
    check(saturated.forced > 0, "saturation forces fades instead of overwriting");

    for (const auto* stream : {&saturation, &sustained})
    {
        const auto dropping = run(*stream, SlotPolicy::drop_new);
        const auto graduating = run(*stream, SlotPolicy::graduate_oldest);
        std::printf("graduate-oldest %.0f/s: %llu started (drop-new %llu), %llu dropped (drop-new %llu), %llu deferred, %zu waiting at the end\n",
            stream->rate * 60, static_cast<unsigned long long>(graduating.started), static_cast<unsigned long long>(dropping.started),
            static_cast<unsigned long long>(graduating.dropped), static_cast<unsigned long long>(dropping.dropped),
            static_cast<unsigned long long>(graduating.deferred), graduating.waiting);
        check(graduating.overwrite_safe && graduating.slot_safe && graduating.fades_smooth && graduating.fades_long && graduating.bounded,
            "graduating early keeps every pool invariant");
        check(graduating.early_clean, "an early graduation frees a slab only after its other layers have died");
        check(graduating.started + graduating.dropped + graduating.waiting == graduating.submitted, "every event starts, waits or is dropped once");
        check(graduating.started >= dropping.started && graduating.dropped <= dropping.dropped, "graduating early starts more events than dropping");
        check(run(*stream, SlotPolicy::graduate_oldest).listings == graduating.listings, "graduating early is deterministic");
    }
    check(run(saturation, SlotPolicy::graduate_oldest).deferred > 0, "saturation defers events to early graduations");

    const Stream short_mixed{24.0 / 60, {ExplosionPreset::small_impact, ExplosionPreset::artillery_he, ExplosionPreset::heavy,
        ExplosionPreset::fuel_fire}, 60 * 90, 53, 10};
    for (const auto policy : {SlotPolicy::drop_new, SlotPolicy::graduate_oldest})
    {
        const auto outcome = run(short_mixed, policy);
        std::printf("short linger %s: %llu started, %llu deferred, %llu forced fades\n", policy == SlotPolicy::drop_new ? "drop-new" : "graduate-oldest",
            static_cast<unsigned long long>(outcome.started), static_cast<unsigned long long>(outcome.deferred),
            static_cast<unsigned long long>(outcome.forced));
        check(outcome.overwrite_safe && outcome.slot_safe && outcome.fades_smooth && outcome.fades_long && outcome.bounded && outcome.early_clean &&
            outcome.refused == 0, "a short linger with early graduations keeps every pool invariant");
    }

    for (const auto* base : {&barrage, &sustained, &saturation, &short_mixed})
    {
        auto split = *base;
        split.split_seed = 97 + base->seed;
        const auto every_tick = run(split, SlotPolicy::drop_new, false);
        const auto by_frame = run(split, SlotPolicy::drop_new, true);
        check(!every_tick.schedules.empty() && every_tick.schedules == by_frame.schedules && every_tick.listings == by_frame.listings,
            "scheduling does not depend on how ticks fall into frames");
    }
    return failures == 0 ? 0 : 1;
}
