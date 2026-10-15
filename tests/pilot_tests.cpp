#include "simulation/pilot_control.hpp"
#include "simulation/jet.hpp"
#include "graphics/particle_layout.hpp"
#include "graphics/realism_layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <span>
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

using namespace pyroclast;

const std::uint32_t all_bits = realism_layout::realism_full_mask;
const WindSettings still{0, 0, 0};
constexpr double degree = rotor::pi / 180;
const HelicopterSettings helicopter{};
const JetSettings jet_defaults{};

double ground_at(const Terrain& terrain, double3 point)
{
    return terrain.height(static_cast<float>(point.x), static_cast<float>(point.z));
}

bool same(double3 a, double3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

struct Keys
{
    std::mt19937 random;
    std::uint32_t left = 0;
    double right = 0;
    double up = 0;
    double forward = 0;
    bool fast = false;

    explicit Keys(std::uint32_t seed) : random(seed) {}
    void next()
    {
        if (left-- != 0) return;
        left = 30 + random() % 90;
        const auto key = [this] { return static_cast<double>(static_cast<int>(random() % 3) - 1); };
        right = key();
        up = key();
        forward = key();
        fast = random() % 2 == 0;
    }
};

PilotScene scene_at(const Terrain& terrain, double x, double z)
{
    return {x, z, ground_at(terrain, {x, 0, z})};
}

double scene_distance(const FlightKnot& knot, const PilotScene& scene)
{
    return std::hypot(knot.position.x - scene.x, knot.position.z - scene.z);
}

void helicopter_limits(const Terrain& terrain)
{
    const auto scene = scene_at(terrain, 50, -30);
    double fastest_climb = 0;
    double hardest = 0;
    double farthest = 0;
    bool above = true;
    bool continuous = true;
    for (std::uint32_t run = 0; run < 12; ++run)
    {
        Keys keys(100 + run);
        HelicopterControl control;
        control.knot.position = {scene.x, scene.surface + 20, scene.z};
        for (std::uint32_t tick = 0; tick < 3600; ++tick)
        {
            keys.next();
            HelicopterInput input{keys.right, keys.up, keys.forward, keys.fast, run * 0.5, scene};
            const double surface = ground_at(terrain, control.knot.position);
            const auto next = step(control, input, surface);
            fastest_climb = std::max(fastest_climb, std::abs(next.knot.velocity.y));
            hardest = std::max(hardest, length(next.knot.acceleration));
            farthest = std::max(farthest, scene_distance(next.knot, scene));
            above &= next.knot.position.y >= surface + rotor::hub_above_skids + pilot::skid_clearance - 0.25;
            continuous &= length(next.knot.velocity - control.knot.velocity) <= helicopter.acceleration / 60 + 1e-9 || next.knot.velocity.y == 0;
            control = next;
        }
    }
    std::printf("helicopter: fastest vertical %.3f m/s, hardest acceleration %.4f m/s^2, farthest %.1f m from the scene\n", fastest_climb, hardest,
        farthest);
    check(hardest <= helicopter.acceleration + 1e-9, "the helicopter accelerates at most helicopter_acceleration");
    check(fastest_climb <= (helicopter.climb_rate * 10 / 6) + 0.3, "the helicopter climbs and sinks at about the climb rate at most");
    check(above, "the helicopter's skids stay over the surface");
    check(farthest <= helicopter.boundary + 15, "the helicopter stays within 15 m of the scene's boundary");
    check(continuous, "the helicopter's velocity changes by at most a tick's acceleration");

    HelicopterControl starting;
    starting.knot.position = {scene.x, 40, scene.z};
    std::uint32_t ticks = 0;
    while (length(starting.knot.velocity) < 0.9 * helicopter.speed && ticks < 60 * 60)
    {
        starting = step(starting, {0, 0, 1, false, 0, scene}, 0);
        ++ticks;
    }
    std::printf("helicopter: 90%% of %.0f m/s after %u ticks\n", helicopter.speed, ticks);
    check(ticks <= 3.5 * 60, "the helicopter reaches 90% of its speed within 3.5 s");

    HelicopterControl moving;
    moving.knot.position = {scene.x, 40, scene.z};
    moving.knot.velocity = {helicopter.fast_speed, 0, 0};
    ticks = 0;
    while (!at_rest(moving) && ticks < 60 * 60)
    {
        moving = step(moving, {.scene = scene}, 0);
        ++ticks;
    }
    std::printf("helicopter: at rest %u ticks after a release at %.0f m/s, %.1f m on\n", ticks, helicopter.fast_speed, moving.knot.position.x - scene.x);
    check(at_rest(moving) && ticks < 15 * 60, "a released helicopter comes to rest within 15 s");
    check(std::abs(moving.knot.position.y - 40) < 0.5, "a released helicopter holds its height");

    HelicopterControl turning;
    turning.knot.position = {scene.x, 40, scene.z};
    double fastest_yaw = 0;
    for (std::uint32_t tick = 0; tick < 600; ++tick)
    {
        turning = step(turning, {0, 0, 1, false, 90 * degree, scene}, 0);
        fastest_yaw = std::max(fastest_yaw, std::abs(turning.knot.yaw_rate));
    }
    check(std::abs(std::remainder(turning.knot.heading - 90 * degree, 2 * rotor::pi)) < 1 * degree,
        "the nose turns to the track (+z) once the helicopter flies along it");
    check(fastest_yaw <= (helicopter.turn_rate * degree) + 1e-9, "the nose turns at most at the yaw rate");

    HelicopterControl returning;
    returning.knot.position = {scene.x - 280, 90, scene.z};
    returning.knot.velocity = {-20, 0, 0};
    ticks = 0;
    const auto home = [&scene](const FlightKnot& knot) {
        return scene_distance(knot, scene) < 5 && std::abs(knot.position.y - scene.surface - pilot::home_height) < 2 && length(knot.velocity) < 1;
    };
    while (!home(returning.knot) && ticks < 60 * 60)
    {
        returning = step(returning, {.scene = scene, .home = true}, ground_at(terrain, returning.knot.position));
        ++ticks;
    }
    std::printf("helicopter: home after %u ticks\n", ticks);
    check(home(returning.knot) && ticks < 30 * 60, "the helicopter flies home within 30 s");
}

void jet_limits(const Terrain& terrain)
{
    const auto scene = scene_at(terrain, 50, -30);
    double lowest = 1e9;
    double steepest = 0;
    double slowest = 1e9;
    double fastest = 0;
    double least_load = 1e9;
    double most_load = 0;
    double farthest = 0;
    for (std::uint32_t run = 0; run < 12; ++run)
    {
        Keys keys(200 + run);
        JetControl control;
        control.knot.position = {-300, 60, 0};
        control.knot.velocity = {jet_defaults.cruise_speed, 0, 0};
        for (std::uint32_t tick = 0; tick < 3600; ++tick)
        {
            keys.next();
            const JetInput input{keys.right, keys.forward > 0 ? -1.0 : keys.forward < 0 ? 0.5 : 0.0, keys.up, keys.fast, scene};
            const double surface = ground_at(terrain, control.knot.position);
            control = step(control, input, surface);
            const double speed = length(control.knot.velocity);
            lowest = std::min(lowest, control.knot.position.y - surface);
            steepest = std::max(steepest, std::abs(std::asin(control.knot.velocity.y / speed)));
            slowest = std::min(slowest, speed);
            fastest = std::max(fastest, speed);
            least_load = std::min(least_load, control.load);
            most_load = std::max(most_load, control.load);
            farthest = std::max(farthest, scene_distance(control.knot, scene));
        }
    }
    std::printf("jet: lowest %.1f m, steepest %.1f deg, %.1f-%.1f m/s, load %.2f-%.2f g, farthest %.0f m from the scene\n", lowest, steepest / degree,
        slowest, fastest, least_load, most_load, farthest);
    check(lowest > pilot::jet_hard_floor, "the jet stays above the hard floor (pulled up before it)");
    check(steepest < pilot::jet_path_limit + 10 * degree, "the jet's climbs and dives stay near the path limit");
    check(slowest > pilot::jet_slowest - 5 && fastest < jet_defaults.top_speed + 5, "the jet's speed stays in its range");
    check(least_load >= pilot::jet_lowest_load - 1e-9 && most_load <= jet_defaults.highest_load + 1e-9, "the jet's load stays in its range");
    check(farthest < 1200, "the jet turns back within 1.2 km of the scene (a turn back from full speed is about 1 km across)");

    const auto ahead = scene_at(terrain, 2000, 0);
    JetControl level;
    level.knot.position = {0, 100, 0};
    level.knot.velocity = {jet_defaults.cruise_speed, 0, 0};
    for (std::uint32_t tick = 0; tick < 600; ++tick) level = step(level, {.scene = ahead}, 0);
    check(std::abs(level.knot.position.y - 100) < 1e-6 && std::abs(level.knot.position.z) < 1e-6 && std::abs(level.load - 1) < 1e-9,
        "with no keys the jet flies straight and level");
    JetControl turn;
    turn.knot.position = {0, 100, 0};
    turn.knot.velocity = {jet_defaults.cruise_speed, 0, 0};
    std::uint32_t half_circle = 0;
    for (std::uint32_t tick = 0; tick < 600; ++tick)
    {
        turn = step(turn, {.roll = 1, .scene = PilotScene{}}, 0);
        if (half_circle == 0 && turn.knot.velocity.x < 0 && std::abs(turn.knot.velocity.z) < 0.05 * jet_defaults.cruise_speed) half_circle = tick + 1;
    }
    std::printf("jet: a held bank turns half a circle in %u ticks, %.1f m lower after 10 s\n", half_circle, 100 - turn.knot.position.y);
    check(half_circle != 0 && half_circle <= 6 * 60, "a held bank turns the jet half a circle within 6 s");
    check(std::abs(turn.bank - (jet_defaults.bank * degree)) < 1e-3 && std::abs(turn.knot.position.y - 100) < 12, "a held D banks the jet right in a level turn");
    const auto recovered = jet_control(turn.knot);
    check(std::abs(recovered.bank - turn.bank) < 1e-3 && std::abs(recovered.load - turn.load) < 1e-3, "jet_control recovers the bank and load");

    JetControl away;
    away.knot.position = {scene.x, 80, scene.z};
    away.knot.velocity = {jet_defaults.cruise_speed, 0, 0};
    double away_farthest = 0;
    double nearest_later = 1e9;
    for (std::uint32_t tick = 0; tick < 60 * 60; ++tick)
    {
        away = step(away, {.scene = scene}, ground_at(terrain, away.knot.position));
        away_farthest = std::max(away_farthest, scene_distance(away.knot, scene));
        if (tick > 20 * 60) nearest_later = std::min(nearest_later, scene_distance(away.knot, scene));
    }
    std::printf("jet: flying away, at most %.0f m out, back within %.0f m\n", away_farthest, nearest_later);
    check(away_farthest < jet_defaults.boundary + 450 && nearest_later < jet_defaults.boundary, "the boundary turns a jet flying away back");

    JetControl returning;
    returning.knot.position = {scene.x + 1500, 120, scene.z};
    returning.knot.velocity = {jet_defaults.top_speed, 0, 0};
    returning.speed_target = jet_defaults.top_speed;
    std::uint32_t ticks = 0;
    while (scene_distance(returning.knot, scene) >= pilot::jet_home_reach && ticks < 60 * 60)
    {
        returning = step(returning, {.scene = scene, .home = true}, ground_at(terrain, returning.knot.position));
        ++ticks;
    }
    std::printf("jet: home after %u ticks\n", ticks);
    check(ticks < 40 * 60, "the jet flies home within 40 s");

    for (std::uint32_t start = 0; start < 4; ++start)
    {
        JetControl circling;
        const double angle = start * 1.7;
        circling.knot.position = {600 * std::cos(angle), 80, 600 * std::sin(angle)};
        circling.knot.velocity = double3{std::cos(angle + 2), 0, std::sin(angle + 2)} * jet_defaults.cruise_speed;
        const JetOrbit orbit{50, -30, 70};
        double worst = 0;
        double worst_height = 0;
        for (std::uint32_t tick = 0; tick < 120 * 60; ++tick)
        {
            circling = orbit_step(circling, orbit, ground_at(terrain, circling.knot.position));
            if (tick < 90 * 60) continue;
            const double distance = std::hypot(circling.knot.position.x - orbit.centre_x, circling.knot.position.z - orbit.centre_z);
            worst = std::max(worst, std::abs(distance - pilot::orbit_radius));
            worst_height = std::max(worst_height, std::abs(circling.knot.position.y - orbit.height));
        }
        std::printf("orbit from %u: radius within %.1f m, height within %.1f m\n", start, worst, worst_height);
        check(worst < 0.1 * pilot::orbit_radius, "the released jet circles within 10% of the orbit's radius");
        check(worst_height < 10, "the released jet holds the orbit's height");
    }
}

void push_and_settings(const Terrain& terrain)
{
    const auto far = scene_at(terrain, 3000, 0);
    JetControl pushing;
    pushing.knot.position = {0, 250, 0};
    pushing.knot.velocity = {jet_defaults.cruise_speed, 0, 0};
    JetPass pass;
    pass.list_tick = 0;
    pass.knots.push_back(pushing.knot);
    double steepest = 0;
    double least_load = 1e9;
    double lowest = 1e9;
    std::uint32_t pushed = 0;
    for (std::uint32_t tick = 0; tick < 12 * 60; ++tick)
    {
        pushing = step(pushing, {.pitch = -1, .scene = far}, ground_at(terrain, pushing.knot.position));
        pass.knots.push_back(pushing.knot);
        const double path = std::asin(pushing.knot.velocity.y / length(pushing.knot.velocity));
        if (tick < 3 * 60) steepest = std::min(steepest, path);
        if (pushed == 0 && pushing.load < -1) pushed = pushing.knot.tick;
        least_load = std::min(least_load, pushing.load);
        lowest = std::min(lowest, pushing.knot.position.y - ground_at(terrain, pushing.knot.position));
    }
    std::printf("jet: W dives to %.1f deg within 3 s, at %.2f g at least, lowest %.1f m\n", steepest / degree, least_load, lowest);
    check(steepest < -20 * degree, "W pitches the jet down past 20 degrees within 3 s");
    check(least_load >= pilot::jet_lowest_load - 1e-9 && least_load < -1, "W pushes to a negative load");
    check(lowest > pilot::jet_hard_floor, "the ground guard still pulls a held dive out");
    const JetFlight flight{"push", {pass}};
    const auto pose = jet_pose(flight, pushed + 5, terrain, still, false, all_bits);
    check(pose.load < 0 && pose.up.y > 0.5, "a pushing jet is drawn upright at a negative load");
    const auto& pushed_knot = flight.passes.front().knots[pushed + 5];
    check(jet_control(pushed_knot).load < 0, "jet_control reads a push as a negative load");

    JetControl banking;
    banking.knot.position = {0, 150, 0};
    banking.knot.velocity = {jet_defaults.cruise_speed, 0, 0};
    JetPass turn;
    turn.list_tick = 0;
    turn.knots.push_back(banking.knot);
    for (std::uint32_t tick = 0; tick < 120; ++tick)
    {
        banking = step(banking, {.roll = 1, .scene = PilotScene{}}, 0);
        turn.knots.push_back(banking.knot);
    }
    const JetFlight turning{"turn", {turn}};
    const auto banked = jet_pose(turning, 120, terrain, still, false, all_bits);
    const auto birth = knot_birth(turning, 120, terrain, still, false, all_bits);
    check(birth && std::abs(dot(birth->right, banked.right) - 1) < 1e-9 && std::abs(birth->right.y) > 0.9,
        "a knot born in a hard bank lies along the banked span");

    PilotSettings settings;
    bool in_range = true;
    for (std::uint32_t index = 0; index < pilot_setting_count; ++index)
    {
        const auto setting = static_cast<PilotSetting>(index);
        const auto& info = pilot_setting_info(setting);
        const double value = pilot_setting(settings, setting);
        in_range &= value >= info.minimum && value <= info.maximum && !info.label.empty() && !info.format.empty();
    }
    check(in_range, "every default setting is within its range");
    check(set_pilot_setting(settings, PilotSetting::helicopter_speed, 1000) && settings.helicopter.speed == 60,
        "a setting is clamped to its range");
    check(!set_pilot_setting(settings, PilotSetting::jet_bank, std::nan("")) && settings.jet.bank == jet_defaults.bank,
        "a setting that is not finite is refused");
    HelicopterSettings slow;
    slow.speed = 10;
    HelicopterControl crawling;
    crawling.knot.position = {0, 40, 0};
    double fastest = 0;
    for (std::uint32_t tick = 0; tick < 10 * 60; ++tick)
    {
        crawling = step(crawling, {0, 0, 1, false, 0}, 0, slow);
        fastest = std::max(fastest, length(crawling.knot.velocity));
    }
    check(fastest <= 10 + 0.05 && fastest > 9.5, "the helicopter's speed setting sets its speed");
    JetSettings gentle;
    gentle.bank = 45;
    const auto half_circle_ticks = [](const JetSettings& jet) {
        JetControl control;
        control.knot.position = {0, 150, 0};
        control.knot.velocity = {jet.cruise_speed, 0, 0};
        for (std::uint32_t tick = 0; tick < 60 * 60; ++tick)
        {
            control = step(control, {.roll = 1, .scene = PilotScene{}}, 0, jet);
            if (control.knot.velocity.x < 0 && std::abs(control.knot.velocity.z) < 0.05 * jet.cruise_speed) return tick + 1;
        }
        return 60u * 60u;
    };
    const auto gentle_ticks = half_circle_ticks(gentle);
    const auto default_ticks = half_circle_ticks(jet_defaults);
    std::printf("jet: half a circle in %u ticks at 45 degrees, %u at the default bank\n", gentle_ticks, default_ticks);
    check(gentle_ticks > 2 * default_ticks, "a gentler bank setting turns the jet more slowly");
}

void takeover()
{
    FlightBuilder builder("test", 0, {0, 30, 0}, {});
    builder.transit_to({200, 30, 50});
    builder.hold(120);
    auto plan = builder.build();
    std::vector<FlightState> before;
    for (std::uint32_t tick = 0; tick <= 400; ++tick) before.push_back(flight_state(plan, tick));
    continue_plan(plan, 400, {});
    bool kept = true;
    for (std::uint32_t tick = 0; tick <= 400; ++tick)
    {
        const auto after = flight_state(plan, tick);
        kept &= length(after.position - before[tick].position) < 1e-6 && length(after.velocity - before[tick].velocity) < 1e-6 &&
            length(after.acceleration - before[tick].acceleration) < 1e-6;
    }
    check(kept && plan.knots.back().tick == 400, "continue_plan keeps a cut segment's ticks");
    FlightPlan long_plan;
    for (std::uint32_t tick = 0; tick <= 6000; ++tick)
    {
        FlightKnot knot;
        knot.tick = tick;
        knot.position = {tick * 0.1, 30, 0};
        knot.velocity = {6, 0, 0};
        long_plan.knots.push_back(knot);
    }
    continue_plan(long_plan, 6000, {});
    Jetcraft jetcraft;
    jetcraft.fly({"test", {straight_pass("first", {0, 40, 0}, {1, 0, 0}, jet::pass_speed, 600, 10),
                     straight_pass("queued", {0, 40, 0}, {1, 0, 0}, jet::pass_speed, 3000, 10)}},
        28000);
    const auto flying = jetcraft.flight()->passes.front();
    const auto free = jetcraft.end_passes(300, 120);
    const auto& ended = jetcraft.flight()->passes;
    bool unchanged = true;
    for (std::uint32_t tick = 0; tick <= 300; ++tick)
    {
        unchanged &= length(flight_state(ended.front(), tick).position - flight_state(flying, tick).position) < 1e-6;
    }
    const auto at_end = flight_state(ended.front(), 420);
    const auto expected = flight_state(flying, 300).position + flight_state(flying, 300).velocity * 2.0;
    check(free == 420 && ended.size() == 1 && ended.front().unlist_tick == 420 && unchanged && length(at_end.position - expected) < 1e-6,
        "end_passes lets the flying pass fly on straight and dissolve, and drops the queued one");
    check(long_plan.knots.front().tick <= 6000 - plan_memory_ticks && long_plan.knots.size() <= plan_memory_ticks + 2,
        "continue_plan drops knots older than plan_memory_ticks");
}

void tape(const Terrain& terrain)
{
    constexpr std::uint32_t steps = particle_layout::maximum_simulation_steps;
    constexpr std::uint32_t ahead = steps + 1;
    std::mt19937 random(7);
    Rotorcraft rotorcraft;
    Jetcraft jetcraft;
    const std::uint32_t start = 600;
    FlightPlan plan;
    plan.list_tick = start;
    FlightKnot hover;
    hover.tick = start;
    hover.position = {0, 20, 0};
    plan.knots.push_back(hover);
    rotorcraft.fly(plan, 26000);
    JetPass pass;
    pass.list_tick = start;
    FlightKnot cruise;
    cruise.tick = start;
    cruise.position = {-400, 40, 0};
    cruise.velocity = {jet_defaults.cruise_speed, 0, 0};
    pass.knots.push_back(cruise);
    jetcraft.fly({"test", {pass}}, 28000);

    std::vector<HelicopterControl> helicopter_tape{HelicopterControl{hover}};
    std::vector<JetControl> jet_tape{JetControl{cruise}};
    HelicopterControl helicopter_reference{hover};
    JetControl jet_reference{cruise};
    std::vector<FlightState> helicopter_committed;
    std::vector<FlightState> jet_committed;
    std::vector<RotorStep> rotor_committed;
    std::vector<JetEmission> emission_committed;
    const double reference = reference_emission();
    std::uint32_t tick = start;
    Keys keys(9);
    bool exact = true;
    while (tick < start + 2400)
    {
        keys.next();
        const HelicopterInput helicopter_keys{keys.right, keys.up, keys.forward, keys.fast, 0.3};
        const JetInput jet_keys{keys.right, keys.forward > 0 ? -0.3 : 0.0, keys.up, keys.fast};
        const auto found_helicopter = std::find_if(helicopter_tape.begin(), helicopter_tape.end(), [tick](const auto& c) { return c.knot.tick == tick; });
        const auto found_jet = std::find_if(jet_tape.begin(), jet_tape.end(), [tick](const auto& c) { return c.knot.tick == tick; });
        exact &= found_helicopter != helicopter_tape.end() && found_jet != jet_tape.end();
        helicopter_tape = {*found_helicopter};
        jet_tape = {*found_jet};
        std::vector<FlightKnot> helicopter_knots;
        std::vector<FlightKnot> jet_knots;
        for (std::uint32_t index = 0; index < ahead; ++index)
        {
            helicopter_tape.push_back(step(helicopter_tape.back(), helicopter_keys, ground_at(terrain, helicopter_tape.back().knot.position)));
            helicopter_knots.push_back(helicopter_tape.back().knot);
            jet_tape.push_back(step(jet_tape.back(), jet_keys, ground_at(terrain, jet_tape.back().knot.position)));
            jet_knots.push_back(jet_tape.back().knot);
        }
        rotorcraft.steer(tick, helicopter_knots);
        jetcraft.steer(tick, jet_knots);
        const std::uint32_t frame_steps = random() % (steps + 1);
        const bool skipped = random() % 5 == 0;
        for (std::uint32_t index = 1; index <= frame_steps; ++index)
        {
            const auto rotor = rotor_step(rotorcraft.helicopter()->plan, tick + index, terrain, still, false, all_bits, reference);
            const auto emission = jet_emission(*jetcraft.flight(), tick + index, terrain, still, false, all_bits, jetcraft.history());
            if (skipped) continue;
            helicopter_reference = step(helicopter_reference, helicopter_keys, ground_at(terrain, helicopter_reference.knot.position));
            jet_reference = step(jet_reference, jet_keys, ground_at(terrain, jet_reference.knot.position));
            const auto helicopter_state = flight_state(rotorcraft.helicopter()->plan, tick + index);
            const auto jet_state = flight_state(jetcraft.flight()->passes.back(), tick + index);
            exact &= same(helicopter_state.position, helicopter_reference.knot.position) &&
                length(helicopter_state.velocity - helicopter_reference.knot.velocity) < 1e-12 &&
                same(jet_state.position, jet_reference.knot.position) && length(jet_state.acceleration - jet_reference.knot.acceleration) < 1e-9;
            helicopter_committed.push_back(helicopter_state);
            jet_committed.push_back(jet_state);
            rotor_committed.push_back(rotor);
            emission_committed.push_back(emission);
        }
        if (!skipped && frame_steps != 0)
        {
            jet_wake(*jetcraft.flight(), tick + frame_steps, terrain, still, false, all_bits, jetcraft.history());
            tick += frame_steps;
        }
    }
    check(exact, "each committed tick is the law integrated with the keys of the frame that committed it");
    bool kept = true;
    bool rotor_kept = true;
    bool emission_kept = true;
    JetHistory fresh;
    for (std::uint32_t index = 0; index < helicopter_committed.size(); ++index)
    {
        const std::uint32_t at = start + 1 + index;
        const auto helicopter_state = flight_state(rotorcraft.helicopter()->plan, at);
        const auto jet_state = flight_state(jetcraft.flight()->passes.back(), at);
        kept &= same(helicopter_state.position, helicopter_committed[index].position) && same(helicopter_state.velocity, helicopter_committed[index].velocity) &&
            same(jet_state.position, jet_committed[index].position) && same(jet_state.acceleration, jet_committed[index].acceleration);
        const auto rotor = rotor_step(rotorcraft.helicopter()->plan, at, terrain, still, false, all_bits, reference);
        rotor_kept &= rotor.emission == rotor_committed[index].emission && same(rotor.hub, rotor_committed[index].hub) &&
            rotor.inflow == rotor_committed[index].inflow;
        const auto emission = jet_emission(*jetcraft.flight(), at, terrain, still, false, all_bits, fresh);
        emission_kept &= emission.dust == emission_committed[index].dust && emission.half_width == emission_committed[index].half_width;
    }
    check(kept, "later frames keep every committed tick's state");
    check(rotor_kept, "every committed tick's rotor step is unchanged");
    check(emission_kept, "every committed tick's jet emission equals one from a fresh wake memo");
    const auto memoised = jet_wake(*jetcraft.flight(), tick, terrain, still, false, all_bits, jetcraft.history());
    JetHistory afresh;
    const auto recomputed = jet_wake(*jetcraft.flight(), tick, terrain, still, false, all_bits, afresh);
    check(memoised.count == recomputed.count &&
            std::memcmp(memoised.knots.data(), recomputed.knots.data(), sizeof(JetKnotRecord) * memoised.count) == 0,
        "the jet's memoised wake equals one computed afresh after rewritten ticks");
}

void taper(const Terrain& terrain)
{
    JetPass pass;
    pass.list_tick = 0;
    JetControl circling;
    circling.knot.position = {pilot::orbit_radius, 80, 0};
    circling.knot.velocity = {0, 0, pilot::orbit_speed};
    pass.knots.push_back(circling.knot);
    const JetOrbit orbit{0, 0, 80};
    for (std::uint32_t tick = 0; tick < 3600; ++tick)
    {
        circling = orbit_step(circling, orbit, ground_at(terrain, circling.knot.position));
        pass.knots.push_back(circling.knot);
    }
    const JetFlight flight{"test", {pass}};
    JetHistory history;
    const auto wake = jet_wake(flight, 3600, terrain, still, false, all_bits, history);
    check(wake.count == jet_layout::jet_knot_limit, "a minute's orbit fills the frame's knots");
    if (wake.count != jet_layout::jet_knot_limit) return;
    const auto last = wake.count - 1;
    const auto full = wake.count - jet::taper_knots - 1;
    check(wake.knots[last].words[0].w < 0.1f * wake.knots[full].words[0].w && wake.knots[last].words[0].w > 0,
        "the oldest knots' circulation tapers towards the limit");
}
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Terrain terrain;
    helicopter_limits(terrain);
    jet_limits(terrain);
    takeover();
    push_and_settings(terrain);
    tape(terrain);
    taper(terrain);
    if (failures == 0) std::printf("pilot tests passed\n");
    return failures == 0 ? 0 : 1;
}
