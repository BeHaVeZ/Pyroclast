#include "simulation/explosion_scenarios.hpp"
#include "simulation/explosion_system.hpp"

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

    const Terrain terrain;
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

    check(!make_scenario("storm", terrain) && !is_scenario_name("storm") && is_scenario_name("barrage"), "unknown scenarios are rejected");
    const auto single = make_scenario("single", terrain);
    const auto lifetime_ticks = effect_ticks(ExplosionPreset::small_he, SurfaceType::dirt);
    check(single && single->events.size() == 1 && single->events[0].tick == 0 && single->duration_ticks == lifetime_ticks,
        "single: one effect for its lifetime");
    const auto grid = make_scenario("grid-100", terrain);
    check(grid && grid->events.size() == 100 && grid->events.back().tick == 0, "grid-100: the startup group at tick 0");
    const auto barrage = make_scenario("barrage", terrain);
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
    const auto again = make_scenario("barrage", terrain);
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
    return failures == 0 ? 0 : 1;
}
