#include "simulation/jet.hpp"
#include "graphics/particle_layout.hpp"
#include "graphics/realism_layout.hpp"

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

using namespace pyroclast;

const std::uint32_t all_bits = realism_layout::realism_full_mask;
const WindSettings still{0, 0, 0};
const std::uint32_t jet_seed_for_tests = 28000;

JetFlight one_pass(double height, std::uint32_t crossing = 600, double speed = jet::pass_speed)
{
    return {"test", {straight_pass("test", {0, height, 0}, {1, 0, 0}, speed, crossing, 10)}};
}

struct Wake
{
    JetWake wake;
    JetHead head;
};

Wake wake_at(const JetFlight& flight, std::uint32_t tick, const Terrain& terrain, JetHistory& history, const WindSettings& wind = still)
{
    Wake result;
    result.wake = jet_wake(flight, tick, terrain, wind, false, all_bits, history);
    result.head = jet_head(jet_pose(flight, tick, terrain, wind, false, all_bits));
    include_head(result.wake, result.head);
    return result;
}

double3 point_of(float4 a)
{
    return {a.x, a.y, a.z};
}
}

int main()
{
    Terrain terrain;
    const double b0 = jet::spacing;
    check(jet::mass == jet_layout::jet_mass && std::abs(jet::spacing - jet_layout::jet_vortex_spacing) < 1e-5 &&
            static_cast<float>(jet::nozzle_aft) == jet_layout::jet_nozzle_aft && static_cast<float>(jet::nozzle_radius) == jet_layout::jet_nozzle_radius,
        "jet's constants are jet_layout's");
    check(terrain.height(0, 0) == 0, "the range's centre is flat at 0");

    {
        FlightBuilder builder("arc", 0, {0, 100, 0}, {160, 0, 0});
        builder.cruise(600);
        builder.arc({0, 0, 1}, jet::pi / 2, 4 * jet::gravity);
        const std::uint32_t arc_end = builder.tick();
        builder.cruise(600);
        const auto plan = builder.build();
        const auto before = flight_state(plan, 300);
        const auto after = flight_state(plan, arc_end + 300);
        check(std::abs(before.position.y - 100) < 1e-6 && std::abs(before.position.z) < 0.05 && std::abs(after.velocity.x) < 0.05 &&
                std::abs(after.position.x - flight_state(plan, arc_end).position.x) < 0.05,
            "the segments around an arc stay straight");
        const auto end = flight_state(plan, arc_end);
        check(std::abs(end.velocity.x) < 1e-6 && std::abs(end.velocity.z - 160) < 1e-6, "an arc turns the velocity by its angle");
        bool steady = true;
        for (std::uint32_t tick = 600; tick <= arc_end; ++tick)
        {
            const auto state = flight_state(plan, tick);
            steady = steady && std::abs(length(state.velocity) - 160) < 0.8;
        }
        check(steady, "an arc keeps its speed within 0.5%");
        const double radius = 160.0 * 160.0 / (4 * jet::gravity);
        const double3 centre{1600, 100, radius};
        const auto middle = flight_state(plan, (600 + arc_end) / 2);
        check(std::abs(length(middle.position - centre) - radius) < 0.5, "an arc stays on its circle");
    }

    {
        const auto flight = one_pass(60);
        const auto pose = jet_pose(flight, 600, terrain, still, false, all_bits);
        check(pose.listed && std::abs(pose.load - 1) < 1e-6 && std::abs(pose.airspeed - jet::pass_speed) < 1e-6, "a level pass flies at 1 g");
        check(pose.forward.x > 0.99 && pose.forward.y > 0 && std::abs(pose.right.z - 1) < 1e-3 && std::abs(pose.right.y) < 1e-6,
            "its nose is ahead and a little up, its wings level");
        check(pose.thrust > 18000 && pose.thrust < 26000, "its thrust balances about 20 kN of drag");
        check(!jet_pose(one_pass(60, 1200), 0, terrain, still, false, all_bits).listed, "before its listing it is not listed");
    }

    {
        const double gamma = 76.5;
        const auto far = pair_rate(500, 0.5 * b0, gamma);
        check(std::abs(-far.height / (gamma / (2 * jet::pi * b0)) - 1) < 1e-3 && std::abs(far.half_spacing) < 1e-3, "far from the ground the pair sinks");
        const auto near = pair_rate(1, 20, gamma);
        check(std::abs(near.half_spacing / (gamma / (4 * jet::pi * 1)) - 1) < 0.01 && std::abs(near.height) < 0.05, "near the ground it spreads");
        check(std::abs(gamma - jet::mass * jet::gravity / (jet::density * jet::pass_speed * b0)) < 0.2, "Gamma0 = W / (rho V b0) = 76.5 m^2/s");
    }

    {
        const auto flight = one_pass(40, 600);
        const auto birth = knot_birth(flight, 600, terrain, still, false, all_bits);
        check(birth.has_value() && std::abs(birth->centre.x) < 1e-6, "a knot is born where the jet is");
        JetHistory stepped;
        for (std::uint32_t age = 0; age <= 7 * 60; ++age) stepped.state(*birth, age, still, false, all_bits);
        const auto stepwise = stepped.state(*birth, 7 * 60, still, false, all_bits);
        JetHistory fresh;
        const auto direct = fresh.state(*birth, 7 * 60, still, false, all_bits);
        check(stepwise.height == direct.height && stepwise.half_spacing == direct.half_spacing, "a knot's state does not depend on the frames");
        const double h0 = 40;
        const double s0 = 0.5 * b0;
        const double invariant = 1 / (h0 * h0) + 1 / (s0 * s0);
        const double now = 1 / (direct.height * direct.height) + 1 / (direct.half_spacing * direct.half_spacing);
        check(std::abs(now / invariant - 1) < 1e-4, "the pair keeps 1 / h^2 + 1 / s^2");
        check(direct.height < h0 - 5 && direct.height > h0 - 15, "it sinks about 1.5 m/s");
    }

    {
        const auto flight = one_pass(12, 600);
        const auto birth = knot_birth(flight, 600, terrain, still, false, all_bits);
        JetHistory history;
        const auto late = history.state(*birth, 20 * 60, still, false, all_bits);
        check(late.half_spacing > 1.5 * 0.5 * b0 && late.height > 0.3 * 0.5 * b0 && late.height < 12, "a low pass's wake spreads along the ground");
    }

    {
        const auto flight = one_pass(60, 600);
        const auto birth = knot_birth(flight, 600, terrain, {5, 90, 0}, false, all_bits);
        JetHistory history;
        const auto state = history.state(*birth, 10 * 60, {5, 90, 0}, false, all_bits);
        check(state.drift.z > 50 && std::abs(state.drift.x) < 0.2 * state.drift.z, "the wind carries the wake");
    }

    {
        const auto flight = one_pass(60, 600);
        JetHistory history;
        const auto at = wake_at(flight, 720, terrain, history);
        check(at.wake.count > 10 && at.wake.count <= jet_layout::jet_knot_limit && at.wake.joined, "the wake has its knots, joined to the wing");
        bool joins = true;
        for (std::uint32_t knot = 0; knot + 1 < at.wake.count; ++knot) joins = joins && at.wake.knots[knot].words[3].x == 1;
        check(joins, "a straight pass's knots are all joined");
        double3 left;
        double3 right;
        double nearest = 1e30;
        for (std::uint32_t knot = 0; knot < at.wake.count; ++knot)
        {
            const double3 l = point_of(at.wake.knots[knot].words[0]);
            if (std::abs(l.x) < nearest)
            {
                nearest = std::abs(l.x);
                left = l;
                right = point_of(at.wake.knots[knot].words[1]);
            }
        }
        const double3 centre = (left + right) * 0.5;
        const auto down = jet_air(at.wake, at.head, centre);
        check(down.y < -3 && down.y > -10, "air sinks between the legs (about 6 m/s)");
        const auto outside = jet_air(at.wake, at.head, right + double3{0, 0, 4});
        check(outside.y > 1, "air rises outside them");
        const auto core = jet_air(at.wake, at.head, right + double3{0, 1.2, 0});
        check(length(core) > 5, "air swirls around a core");
        check(length(jet_air(at.wake, at.head, centre + double3{0, 100, 0})) == 0, "air far from the wake is still");
        const auto pose = jet_pose(flight, 720, terrain, still, false, all_bits);
        const auto ahead = jet_air(at.wake, at.head, pose.centre + double3{6, 0, 0});
        check(ahead.y > 0.5, "air rises ahead of the wing");
        const auto exhaust = jet_air(at.wake, at.head, pose.centre - double3{jet::nozzle_aft + 3, 0, 0});
        check(exhaust.x < -5, "the exhaust blows backwards");
        check(length(jet_air(at.wake, at.head, pose.centre + double3{0, 0, 4})) <= jet_layout::jet_speed_limit + 1e-6, "the air is capped");
    }

    {
        const auto flight = one_pass(60, 600);
        JetHistory history;
        const auto before = wake_at(flight, 600 - 300, terrain, history);
        check(before.wake.count == 0, "no knots before the jet reaches the range");
        JetHistory late;
        const auto after = wake_at(flight, 1300, terrain, late);
        check(after.head.words[0].w == 0 && after.wake.count > 0, "after its listing the wake stays, the head goes");
    }

    {
        bool cycles = true;
        for (std::uint32_t local = 0; local < particle_layout::explosion_particles; ++local)
        {
            const auto cycle = jet_cycle(jet_seed_for_tests, local);
            cycles = cycles && cycle.length >= 300 && cycle.length <= 720 && cycle.phase < cycle.length;
        }
        check(cycles, "the jet's records live 5-12 s cycles");
        const auto flight = one_pass(60, 600);
        JetHistory history;
        const auto high = jet_emission(flight, 600, terrain, still, false, all_bits, history);
        check(high.exhaust == jet_layout::jet_exhaust_emission && high.dust == 0, "a pass at 60 m trails exhaust and raises no dust");
        check(jet_emission(flight, 300, terrain, still, false, all_bits, history).exhaust == 0, "no exhaust before the range");
        const auto low = one_pass(8, 600);
        JetHistory low_history;
        std::uint32_t most = 0;
        float width = 0;
        for (std::uint32_t tick = 600; tick <= 1500; tick += 30)
        {
            const auto emission = jet_emission(low, tick, terrain, still, false, all_bits, low_history);
            if (emission.dust > most)
            {
                most = emission.dust;
                width = emission.half_width;
            }
        }
        check(most > 100 && width > 2, "a pass at 8 m raises dust under its pair");
        JetEmissionHistory emissions;
        JetEmission steady;
        steady.exhaust = jet_layout::jet_exhaust_emission;
        for (std::uint32_t tick = 0; tick <= 2000; ++tick) emissions.store(tick, steady, {0, 0, 0});
        const auto census = jet_census(emissions, jet_seed_for_tests, 0, never, 2000, true, true);
        const double expected = jet_layout::jet_exhaust_records * jet_layout::jet_exhaust_emission / 1024.0;
        check(census.dust == 0 && std::abs(census.exhaust - expected) < 0.08 * expected, "the census counts the steady exhaust's records");
        check(jet_census(emissions, jet_seed_for_tests, 0, never, 2000, false, true).exhaust == 0, "a hidden layer counts nothing");
    }

    if (failures == 0) std::printf("All jet tests passed.\n");
    return failures == 0 ? 0 : 1;
}
