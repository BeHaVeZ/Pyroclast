#include "simulation/explosion_scenarios.hpp"
#include "simulation/explosion_system.hpp"

#include <tuple>

#include <algorithm>
#include <cmath>
#include <cstdio>
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
}

int main()
{
    using namespace pyroclast;
    ExplosionSystem system(3);
    check(system.capacity() == 3 && system.active().empty(), "an empty system");

    system.submit({{1, 0, 2}, {0, 1, 0}, 7});
    system.submit({{5, 0, 5}, {0, 1, 0}, 8});
    check(system.pending() == 2 && system.active().empty(), "submitting only queues");
    system.start(10);
    check(system.active().size() == 2 && system.active()[0] == 0 && system.active()[1] == 1 && system.pending() == 0,
        "queued events take the lowest free slots");
    const auto& first = system.slots()[0];
    check(first.active && first.seed == 7 && first.start_tick == 10 && first.generation == 1 && first.origin.x == 1 && first.origin.z == 2,
        "a slot records its event, start tick and generation");

    for (std::uint32_t seed = 0; seed < 3; ++seed) system.submit({{0, 0, 0}, {0, 1, 0}, 100 + seed});
    system.start(20);
    check(system.active().size() == 3 && system.dropped() == 2 && system.started() == 3, "a full system drops and counts");
    check(system.slots()[2].seed == 100 && system.slots()[2].start_tick == 20, "the free slot takes the first queued event");

    const auto lifetime = effect_ticks(ExplosionPreset::small_he, SurfaceType::dirt);
    check(system.slots()[0].lifetime_ticks == lifetime && system.slots()[0].effect_class == effect_class_index(ExplosionPreset::small_he, SurfaceType::dirt),
        "a slot records its event's class and lifetime");
    system.start(10 + lifetime - 1);
    check(system.active().size() == 3, "an effect lives for effect_ticks");
    system.submit({{9, 0, 9}, {0, 1, 0}, 42});
    system.start(10 + lifetime);
    check(system.active().size() == 2 && system.active()[0] == 0 && system.active()[1] == 2,
        "the two effects that started at tick 10 retire; the queued event takes slot 0");
    check(system.slots()[0].seed == 42 && system.slots()[0].generation == 2 && !system.slots()[1].active,
        "a reused slot gets the next generation");

    system.submit({{0, 0, 0}, {0, 1, 0}, 1});
    system.clear();
    check(system.active().empty() && system.pending() == 0 && !system.slots()[0].active, "clear retires and forgets");
    system.submit({{0, 0, 0}, {0, 1, 0}, 2});
    system.start(1000);
    check(system.active().size() == 1 && system.slots()[0].generation == 3, "a slot cleared and reused gets a new generation");

    const auto run = [] {
        ExplosionSystem copy(4);
        std::vector<std::uint32_t> trace;
        for (std::uint32_t tick = 0; tick < 2000; tick += 37)
        {
            copy.submit({{static_cast<float>(tick), 0, 0}, {0, 1, 0}, tick});
            copy.start(tick);
            for (const auto index : copy.active()) trace.push_back(index * 100000 + copy.slots()[index].generation);
        }
        return trace;
    };
    check(run() == run(), "the same script gives the same slots and generations");

    const Environment environment;
    const auto& terrain = environment.terrain();
    for (const auto count : {1u, 10u, 50u, 100u})
    {
        const auto group = group_events(terrain, count);
        float sum_x = 0;
        float sum_z = 0;
        bool grounded = group.size() == count;
        for (std::uint32_t index = 0; index < group.size() && grounded; ++index)
        {
            const auto& event = group[index];
            sum_x += event.position.x;
            sum_z += event.position.z;
            grounded = event.seed == index && event.normal.y > 0.999f &&
                std::abs(event.position.y - (terrain.height(event.position.x, event.position.z) + 0.5f)) < 1e-4f;
        }
        check(grounded && std::abs(sum_x) < 1e-3f && std::abs(sum_z) < 1e-3f &&
            (count == 1 || group[1].position.x - group[0].position.x == group_spacing), "a startup group is a centred grid on the ground");
    }

    check(!make_scenario("storm", environment) && !is_scenario_name("storm") && is_scenario_name("barrage"), "unknown scenarios are rejected");
    const auto single = make_scenario("single", environment);
    const auto lifetime_ticks = effect_ticks(ExplosionPreset::small_he, SurfaceType::dirt);
    check(single && single->events.size() == 1 && single->events[0].tick == 0 && single->duration_ticks == lifetime_ticks,
        "single: one effect for its lifetime");
    const auto grid = make_scenario("grid-100", environment);
    check(grid && grid->events.size() == 100 && grid->events.back().tick == 0, "grid-100: the startup group at tick 0");
    const auto barrage = make_scenario("barrage", environment);
    bool walking = barrage && barrage->events.size() == 80;
    for (std::size_t index = 0; walking && index < barrage->events.size(); ++index)
    {
        const auto& scripted = barrage->events[index];
        walking = scripted.tick == index * 15 && std::abs(scripted.event.position.x) <= 64 && std::abs(scripted.event.position.z) <= 12 &&
            (index < 8 || scripted.event.position.x > barrage->events[index - 8].event.position.x);
    }
    check(walking && barrage->events.front().event.position.x < -50 && barrage->events.back().event.position.x > 50 &&
        barrage->duration_ticks == 79 * 15 + lifetime_ticks,
        "barrage: 80 impacts 15 ticks apart, walking east along the range");
    const auto again = make_scenario("barrage", environment);
    bool same = again && again->events.size() == barrage->events.size();
    for (std::size_t index = 0; same && index < again->events.size(); ++index)
    {
        same = again->events[index].event.position.x == barrage->events[index].event.position.x &&
            again->events[index].event.seed == barrage->events[index].event.seed;
    }
    check(same, "scenarios are deterministic");
    ExplosionSystem slots(128);
    std::size_t peak = 0;
    std::size_t next = 0;
    for (std::uint32_t tick = 0; tick < barrage->duration_ticks; ++tick)
    {
        while (next < barrage->events.size() && barrage->events[next].tick <= tick) slots.submit(barrage->events[next++].event);
        slots.start(tick);
        peak = std::max(peak, slots.active().size());
    }
    check(peak == 32 && slots.dropped() == 0, "the barrage peaks at 32 effects and drops none");

    const auto heavy_barrage = make_scenario("barrage", environment, ExplosionPreset::heavy);
    bool surfaced = heavy_barrage && heavy_barrage->events.size() == 80;
    bool concrete = false;
    for (std::size_t index = 0; surfaced && index < heavy_barrage->events.size(); ++index)
    {
        const auto& event = heavy_barrage->events[index].event;
        surfaced = event.preset == ExplosionPreset::heavy && event.surface == Terrain::surface(event.position.x, event.position.z);
        concrete = concrete || event.surface == SurfaceType::concrete;
    }
    check(surfaced && concrete && heavy_barrage->duration_ticks == 79 * 15 + effect_ticks(ExplosionPreset::heavy, SurfaceType::dirt),
        "scripted events carry the preset and the surface under them; the barrage crosses the concrete pad");
    const auto presets = make_scenario("presets", environment, ExplosionPreset::heavy);
    bool row = presets && presets->events.size() == preset_count && is_scenario_name("presets");
    std::uint32_t longest = 0;
    for (std::uint32_t index = 0; row && index < presets->events.size(); ++index)
    {
        const auto& scripted = presets->events[index];
        row = scripted.tick == 0 && scripted.event.preset == static_cast<ExplosionPreset>(index) && scripted.event.surface == SurfaceType::dirt &&
            (index == 0 || scripted.event.position.x > presets->events[index - 1].event.position.x);
        longest = std::max(longest, effect_ticks(scripted.event.preset, SurfaceType::dirt));
    }
    check(row && presets->duration_ticks == longest, "presets: every preset once, west to east, on dirt, until the longest-lived ends");
    const auto bombardment = make_scenario("bombardment", environment);
    bool disc = bombardment && bombardment->events.size() == 400 && is_scenario_name("bombardment");
    for (std::size_t index = 0; disc && index < bombardment->events.size(); ++index)
    {
        const auto& scripted = bombardment->events[index];
        const auto& position = scripted.event.position;
        disc = scripted.tick == index * 3 && scripted.event.seed == 4000 + index && std::sqrt(position.x * position.x + position.z * position.z) <= 30.001f;
    }
    check(disc && bombardment->duration_ticks == 399 * 3 + effect_ticks(ExplosionPreset::small_he, SurfaceType::dirt),
        "bombardment: 400 impacts every 3 ticks within 30 m of the origin");
    ExplosionSystem saturated(128);
    std::size_t busiest = 0;
    next = 0;
    for (std::uint32_t tick = 0; tick < bombardment->duration_ticks; ++tick)
    {
        while (next < bombardment->events.size() && bombardment->events[next].tick <= tick) saturated.submit(bombardment->events[next++].event);
        saturated.start(tick);
        busiest = std::max(busiest, saturated.active().size());
    }
    std::printf("bombardment: peak %zu effects, %llu dropped of 400\n", busiest, static_cast<unsigned long long>(saturated.dropped()));
    const auto windows = [&](ExplosionPreset preset, SlotPolicy policy, const char* name = "bombardment") {
        const auto script = make_scenario(name, environment, preset);
        ExplosionSystem system(128);
        std::size_t submitted = 0;
        std::uint64_t last_dropped = 0;
        std::uint32_t run = 0;
        std::uint32_t longest_run = 0;
        std::uint32_t last_start = 0;
        std::uint32_t longest_gap = 0;
        std::uint64_t last_started = 0;
        for (std::uint32_t tick = 0; tick < script->duration_ticks; ++tick)
        {
            while (submitted < script->events.size() && script->events[submitted].tick <= tick) system.submit(script->events[submitted++].event);
            system.start(tick, policy);
            if (system.dropped() > last_dropped) run += static_cast<std::uint32_t>(system.dropped() - last_dropped);
            else if (system.started() > last_started) run = 0;
            longest_run = std::max(longest_run, run);
            if (system.started() > last_started)
            {
                longest_gap = std::max(longest_gap, tick - last_start);
                last_start = tick;
            }
            last_dropped = system.dropped();
            last_started = system.started();
        }
        return std::tuple{system.dropped(), system.evicted(), longest_run, longest_gap, system.started()};
    };
    const auto [small_dropped, small_evicted, small_run, small_gap, small_started] = windows(ExplosionPreset::small_he, SlotPolicy::drop_new);
    const auto [art_dropped, art_evicted, art_run, art_gap, art_started] = windows(ExplosionPreset::artillery_he, SlotPolicy::drop_new);
    std::printf("bombardment drop-new: small-he %llu dropped (runs of %u, %u ticks without a start), artillery-he %llu (runs of %u, %u ticks)\n",
        static_cast<unsigned long long>(small_dropped), small_run, small_gap, static_cast<unsigned long long>(art_dropped), art_run, art_gap);
    check(small_dropped == 64 && art_dropped == 144 && small_evicted == 0 && art_evicted == 0 && small_run == 32 && art_run == 72 &&
        small_gap == 99 && art_gap == 219, "drop-new drops 64 small-he and 144 artillery-he impacts in runs of 32 and 72");
    const auto [kept_dropped, kept_evicted, kept_run, kept_gap, kept_started] = windows(ExplosionPreset::artillery_he, SlotPolicy::evict_oldest);
    std::printf("bombardment evict-oldest: artillery-he %llu evicted, %llu dropped\n", static_cast<unsigned long long>(kept_evicted),
        static_cast<unsigned long long>(kept_dropped));
    check(kept_dropped == 0 && kept_evicted > 0 && kept_run == 0 && kept_gap <= 3 && kept_started == 400,
        "evict-oldest starts every impact, replacing the oldest effects");
    for (const char* name : {"barrage", "grid-100"})
    {
        const auto dropping = windows(ExplosionPreset::heavy, SlotPolicy::drop_new, name);
        const auto evicting = windows(ExplosionPreset::heavy, SlotPolicy::evict_oldest, name);
        check(dropping == evicting && std::get<0>(dropping) == 0 && std::get<1>(evicting) == 0, "unsaturated scripts ignore the slot policy");
    }
    bool accumulates = true;
    for (std::uint32_t index = 0; index < preset_count; ++index)
    {
        const auto preset = static_cast<ExplosionPreset>(index);
        const auto script = make_scenario("accumulation", environment, preset);
        const auto life = std::max(effect_ticks(preset, SurfaceType::dirt), effect_ticks(preset, SurfaceType::concrete));
        const auto interval = accumulation_interval(preset);
        const auto impacts = (life + accumulation_hold_ticks + interval - 1) / interval;
        ExplosionSystem accumulating(128);
        std::size_t submitted = 0;
        std::size_t most = 0;
        for (std::uint32_t tick = 0; tick < script->duration_ticks; ++tick)
        {
            while (submitted < script->events.size() && script->events[submitted].tick <= tick) accumulating.submit(script->events[submitted++].event);
            accumulating.start(tick);
            most = std::max(most, accumulating.active().size());
        }
        const auto dropping = windows(preset, SlotPolicy::drop_new, "accumulation");
        const auto evicting = windows(preset, SlotPolicy::evict_oldest, "accumulation");
        accumulates &= script->events.size() == impacts && accumulating.started() == impacts && accumulating.dropped() == 0 &&
            most == (life + interval - 1) / interval && most <= 128 && script->duration_ticks == (impacts - 1) * interval + life &&
            dropping == evicting && std::get<1>(evicting) == 0;
        std::printf("accumulation %s: life %u, every %u ticks, %u impacts, peak %zu effects, %u ticks\n", preset_name(preset).data(), life,
            interval, impacts, most, script->duration_ticks);
    }
    check(accumulates, "accumulation never fills more slots than its interval allows, under either policy");
    ExplosionSystem crowded(128);
    for (std::uint32_t index = 0; index < 129; ++index) crowded.submit({{static_cast<float>(index), 0, 0}, {0, 1, 0}, index, ExplosionPreset::small_he, SurfaceType::dirt});
    crowded.start(0, SlotPolicy::evict_oldest);
    const auto first_generation = crowded.slots()[0].generation;
    crowded.submit({{1000, 0, 0}, {0, 1, 0}, 999, ExplosionPreset::small_he, SurfaceType::dirt});
    crowded.start(1, SlotPolicy::evict_oldest);
    check(crowded.dropped() == 1 && crowded.evicted() == 1 && crowded.slots()[0].seed == 999 && crowded.slots()[0].generation == first_generation + 1 &&
        crowded.slots()[0].start_tick == 1 && crowded.active().size() == 128, "an evicted slot restarts with a new generation; same-tick starts stay");
    check(busiest == 128 && saturated.dropped() == 64 && saturated.started() + saturated.dropped() == 400,
        "the bombardment fills the 128 slots and drops the rest");
    return failures == 0 ? 0 : 1;
}
