#include "simulation/rotorcraft.hpp"
#include "graphics/realism_layout.hpp"
#include "graphics/particle_layout.hpp"

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

FlightPlan hover_plan(double3 position)
{
    FlightBuilder builder("hover", 0, position, {});
    builder.hold(6000);
    return builder.build();
}

RotorStep hover_step(const Terrain& terrain, double height, double reference, std::uint32_t mask = all_bits)
{
    return rotor_step(hover_plan({0, height, 0}), 300, terrain, still, false, mask, reference);
}

double emission_share(const RotorStep& step, double reference)
{
    return step.raw_emission / reference;
}

// Kept in step with the wake's skew in rotorcraft.cpp.
double skew_offset(double z)
{
    const double blend = rotor_layout::rotor_skew_blend * rotor::radius;
    return 0.5 * (z + std::sqrt(z * z + blend * blend));
}

double skew_slope(double z)
{
    const double blend = rotor_layout::rotor_skew_blend * rotor::radius;
    return 0.5 * (1 + z / std::sqrt(z * z + blend * blend));
}

std::uint32_t next_random(std::uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return state >> 24;
}
}

int main()
{
    Terrain terrain;
    const double R = rotor::radius;
    const double reference = reference_emission();

    check(rotor::radius == rotor_layout::rotor_radius && rotor::mass == rotor_layout::rotor_mass &&
            static_cast<float>(rotor::density) == rotor_layout::rotor_air_density && static_cast<float>(rotor::gravity) == rotor_layout::rotor_gravity,
        "rotorcraft's constants are rotor_layout's");
    check(std::abs(rotor::hover_inflow - std::sqrt(rotor::thrust / (2 * rotor::density * rotor::area))) < 1e-9 && std::abs(rotor::hover_inflow - 14.11) < 0.01 &&
            std::abs(rotor::area * rotor::hover_inflow - 2837) < 1,
        "v_h = sqrt(T / 2 rho A) = 14.11 m/s and A v_h = 2,837 m^3/s");
    check(terrain.height(0, 0) == 0 && terrain.height(40, -40) == 0, "the range's centre is flat at 0");

    {
        const auto step = hover_step(terrain, 60, reference);
        const double through = -2 * rotor::pi * rotor_stream(step, 2.5 * R * 2.5 * R, 0, 60);
        check(step.listed && std::abs(step.inflow - rotor::hover_inflow) < 0.05 && std::abs(through / (rotor::area * step.inflow) - 1) < 0.02,
            "the flow through the disc plane is pi R^2 v_d within 2%");
        const auto axis = rotor_frame_velocity(step, 0, 2 * R, 60 - 2 * R);
        const double wake = -axis.vertical / step.inflow;
        check(wake > 1.75 && wake < 1.95, "the wake's axial speed nears 2 v by 2 R");
        const auto edge = rotor_frame_velocity(step, 36, 2 * R, 60 - 2 * R);
        const double ratio = edge.vertical / axis.vertical;
        check(ratio > 0.35 && ratio < 0.65, "the slipstream's edge is at the wake's radius");
        const auto above = rotor_frame_velocity(step, 0, -R, 60 + R);
        check(above.vertical < 0 && -above.vertical < 0.5 * step.inflow, "the disc draws the air above it down, more slowly");
    }

    for (const double height : {4.3, R, 1.5 * R, 2 * R, 3 * R})
    {
        const auto step = hover_step(terrain, height, reference);
        const double flux = -2 * rotor::pi * rotor_stream(step, 25 * step.jet_radius * step.jet_radius, step.height, 1e6);
        check(std::abs(rotor::density * flux * flux / (rotor::pi * step.jet_radius * step.jet_radius) / step.effective_thrust - 1) < 0.01,
            "the ground jet carries the effective thrust's momentum");
        bool still_ground = true;
        for (double r = 1; r < 10 * R; r += 3.7) still_ground = still_ground && rotor_frame_velocity(step, r * r, height, 0).vertical == 0;
        check(still_ground, "no air crosses the ground");
        auto wake = step;
        wake.ring = 0;
        double peak = 0;
        for (double r = 0.25; r < 12 * R; r += 0.25) peak = std::max(peak, std::abs(rotor_frame_velocity(wake, r * r, height - 0.5, 0.5).radial));
        const double ground_speed = std::sqrt(step.effective_thrust / (rotor::density * rotor::pi)) / step.jet_radius;
        check(peak > 0 && peak <= ground_speed * 1.05 && std::abs(peak - step.peak_outwash) < 0.3, "the outwash's peak stays under the wake's speed at the ground");
        if (height == R)
        {
            const double near = std::abs(rotor_frame_velocity(wake, 4 * R * R, height - 0.5, 0.5).radial) * 2 * R;
            bool inverse = true;
            for (const double r : {3 * R, 4 * R, 6 * R, 8 * R, 10 * R})
            {
                const double product = std::abs(rotor_frame_velocity(wake, r * r, height - 0.5, 0.5).radial) * r;
                inverse = inverse && std::abs(product / near - 1) < 0.1;
            }
            check(inverse, "the outwash times r is constant within 10% from 2 R to 10 R");
            const double centre = rotor_layout::rotor_ring_radius * R;
            const double core = rotor_layout::rotor_ring_core * R;
            const double y_v = rotor_layout::rotor_ring_height * height;
            auto ring_only = step;
            ring_only.inflow = 1e-9;
            ring_only.jet_flow = 0;
            const auto outside = rotor_frame_velocity(ring_only, (centre + 0.7 * core) * (centre + 0.7 * core), height - y_v, y_v);
            const auto inside = rotor_frame_velocity(ring_only, (centre - 0.7 * core) * (centre - 0.7 * core), height - y_v, y_v);
            const auto over = rotor_frame_velocity(ring_only, centre * centre, height - y_v - 0.7 * core, y_v + 0.7 * core);
            check(step.ring > 0 && outside.vertical > 0 && inside.vertical < 0 && over.radial < 0, "the ring turns down inside, up outside and in over the top");
        }
    }

    {
        const auto step = hover_step(terrain, 1.5 * R, reference);
        double worst = 0;
        const double h = 0.05;
        for (double x = -30; x <= 30; x += 7.3)
        {
            for (double z = -30; z <= 30; z += 6.9)
            {
                for (double y = 1.5; y <= 30; y += 4.1)
                {
                    const double3 p{x, y, z};
                    const double3 u = rotor_air(step, p, 0);
                    const double speed = length(u);
                    if (speed < 0.5) continue;
                    const double divergence = (rotor_air(step, {x + h, y, z}, 0).x - rotor_air(step, {x - h, y, z}, 0).x +
                                                  rotor_air(step, {x, y + h, z}, 0).y - rotor_air(step, {x, y - h, z}, 0).y +
                                                  rotor_air(step, {x, y, z + h}, 0).z - rotor_air(step, {x, y, z - h}, 0).z) /
                        (2 * h);
                    worst = std::max(worst, std::abs(divergence) / (speed / R));
                }
            }
        }
        check(worst < 0.01, "the rotor's air is divergence-free without skew");
    }

    {
        FlightBuilder builder("pass", 0, {-100, 40, 0}, {10, 0, 0});
        builder.cruise(1200);
        const auto step = rotor_step(builder.build(), 600, terrain, still, false, all_bits, reference);
        bool falling = true;
        for (const double depth : {R, 2 * R, 3 * R})
        {
            const double3 point{step.hub.x + step.skew_x * skew_offset(depth), step.hub.y - depth, step.hub.z + step.skew_z * skew_offset(depth) + 1};
            const double3 air = rotor_air(step, point, 0);
            falling = falling && air.y < -10 && std::hypot(air.x, air.z) < 0.2 * -air.y;
        }
        check(falling, "a pass's wake falls without streaming back");
        const std::uint32_t unsheared = all_bits & ~(1u << realism_layout::shear_mask_bit);
        const WindSettings breeze{5, 0, 0};
        const auto hover = rotor_step(hover_plan({0, 60, 0}), 300, terrain, breeze, false, unsheared, reference);
        bool carried = hover.skew_x > 0.2;
        for (const double depth : {0.5 * R, R, 2 * R, 3 * R, 4 * R})
        {
            const double3 point{hover.hub.x + hover.skew_x * skew_offset(depth), hover.hub.y - depth, hover.hub.z};
            const double3 air = rotor_air(hover, point, 0);
            const double across = breeze.speed + air.x + hover.skew_x * skew_slope(depth) * air.y;
            carried = carried && std::abs(air.x) < 1e-6 && std::abs(across) < 0.35 * breeze.speed;
        }
        check(carried, "a wind carries the column's air along the wake's skew");
    }

    {
        double fastest = 0;
        for (const double height : {4.3, R, 2 * R, 50.0})
        {
            const auto step = hover_step(terrain, height, reference);
            for (double r = 0; r < 12 * R; r += 1.1)
            {
                for (double y = 0.5; y < height + 6 * R; y += 0.9) fastest = std::max(fastest, length(rotor_air(step, {r, y, 0}, 0)));
            }
        }
        check(fastest <= rotor_layout::rotor_speed_limit + 1e-6 && fastest + 67.6 < 114, "the rotor's air stays within 40 m/s");
    }

    {
        double previous = 2;
        bool band = true;
        for (const double height : {4.3, R, 1.5 * R, 2 * R, 3 * R})
        {
            const double share = emission_share(hover_step(terrain, height, reference), reference);
            band = band && share >= 0.7 && share <= 1.0 + 1e-9;
        }
        check(band, "the brownout's emission is 0.7-1 of the reference for hovers up to 3 R");
        bool falling = true;
        for (const double height : {3 * R, 4 * R, 5 * R, 50.0})
        {
            const double share = emission_share(hover_step(terrain, height, reference), reference);
            falling = falling && share <= previous + 1e-9;
            previous = share;
        }
        check(falling, "the brownout weakens above 3 R");
        check(hover_step(terrain, 2 * R, reference).emission == rotor_layout::rotor_emission_one, "the reference hover emits 1,024");
        bool lowering = hover_step(terrain, 3 * R, reference).emission > 600 && hover_step(terrain, 6 * R, reference).emission == 0 &&
            hover_step(terrain, 70, reference).emission == 0;
        std::uint32_t previous_emission = rotor_layout::rotor_emission_one + 1;
        for (const double height : {3 * R, 3.5 * R, 4 * R, 4.5 * R, 5 * R, 5.5 * R, 6 * R})
        {
            const auto emission = hover_step(terrain, height, reference).emission;
            lowering = lowering && emission < previous_emission;
            previous_emission = emission;
        }
        check(lowering, "the brownout fades out between 1.5 and 3 rotor diameters up, and a 70 m hover raises none");
        check(hover_step(terrain, 2 * R, reference, all_bits & ~(1u << realism_layout::brownout_mask_bit)).emission == 0 &&
                hover_step(terrain, 2 * R, reference, all_bits & ~(1u << realism_layout::rotor_mask_bit)).emission == 0 &&
                hover_step(terrain, 2 * R, reference, all_bits & ~(1u << realism_layout::rotor_mask_bit)).inflow == 0,
            "the rotor and brownout bits switch the air and the dust off");
        const auto off_grid = rotor_step(hover_plan({250, 2 * R + terrain.height(250, 0), 0}), 300, terrain, still, false, all_bits, reference);
        check(off_grid.emission == 0, "no dust rises beyond the terrain's grid");
        FlightBuilder builder("landed", 0, {0, 30, 0}, {});
        builder.land(terrain, 300);
        const auto landed = builder.build();
        const auto idle = rotor_step(landed, landed.touchdown_tick + 300, terrain, still, false, all_bits, reference);
        check(idle.emission == 0 && idle.peak_outwash < rotor::saltation_threshold, "an idling helicopter on the ground raises no dust");
        const auto descending = rotor_step(landed, landed.touchdown_tick - 120, terrain, still, false, all_bits, reference);
        check(descending.emission > 700, "a landing raises dust");
        const auto step = hover_step(terrain, R, reference);
        check(step.radii_cm[0] <= step.radii_cm[1] && step.radii_cm[1] <= step.radii_cm[2] && step.radii_cm[2] <= step.radii_cm[3] &&
                step.radii_cm[3] <= static_cast<std::uint32_t>(rotor::emission_reach * R * 100) + 1 && step.radii_cm[3] > 0,
            "the dust's radii are ordered and within reach");
    }

    {
        const auto step = hover_step(terrain, R, reference);
        const double centre = rotor_layout::rotor_ring_radius * R;
        double3 position{centre + 2, rotor_layout::rotor_ring_height * R, 0};
        double3 velocity{};
        double angle = 0;
        double previous = std::atan2(position.y - rotor_layout::rotor_ring_height * R, position.x - centre);
        for (int tick = 0; tick < 60 * 20; ++tick)
        {
            const double3 air = rotor_air(step, position, 0);
            velocity = velocity + (air - velocity) * (rotor_layout::rotor_coupling / 60.0);
            position = position + velocity * (1 / 60.0);
            position.y = std::max(position.y, 1.0);
            const double now = std::atan2(position.y - rotor_layout::rotor_ring_height * R, position.x - centre);
            double turn = now - previous;
            if (turn > rotor::pi) turn -= 2 * rotor::pi;
            if (turn < -rotor::pi) turn += 2 * rotor::pi;
            angle += turn;
            previous = now;
        }
        check(std::abs(angle) >= 2 * rotor::pi, "a dust puff in the ring completes a loop");
    }

    {
        FlightBuilder builder("pass", 0, {-100, 40, 0}, {10, 0, 0});
        builder.cruise(1200);
        const auto plan = builder.build();
        const auto step = rotor_step(plan, 600, terrain, still, false, all_bits, reference);
        const double v = step.inflow;
        check(std::abs(v - 12.46) < 0.1 && std::abs(step.skew_x + 10 / (1.5 * 12.46)) < 0.02 && std::abs(step.skew_z) < 1e-9,
            "Glauert's inflow at 10 m/s and the wake's skew backwards");
        check(std::abs(step.effective_thrust / rotor::thrust - 12.46 / std::sqrt(100 + 12.46 * 12.46)) < 0.01, "the wake carries less thrust in forward flight");
    }

    {
        bool cycles = true;
        for (std::uint32_t local = rotor_layout::rotor_disc_records; local < particle_layout::explosion_particles; ++local)
        {
            const auto cycle = dust_cycle(77, local);
            cycles = cycles && cycle.length >= 240 && cycle.length <= 600 && cycle.phase < cycle.length;
        }
        check(cycles, "dust cycles last 4-10 s");
        RotorHistory history;
        RotorStep steady;
        steady.emission = 512;
        steady.radii_cm = {0, 100, 200, 400};
        for (std::uint32_t tick = 4001; tick <= 9000; ++tick) history.store(tick, steady, {});
        const auto census = dust_census(history, 77, 4000, never, 9000, {}, true);
        std::printf("census at a steady emission of 512: %u alive of 2,558 (%.3f of e = 0.5)\n", census.alive, census.alive / (2558.0 * 0.5));
        check(std::abs(census.alive / (2558.0 * 0.5) - 1) < 0.05 && std::abs(census.mean_outer - 4.0) < 1e-9, "a steady emission keeps 2,558 e records alive");
        check(dust_census(history, 77, 4000, never, 9000, {}, false).alive == 0 && dust_census(history, 77, 4000, 9000, 9000, {}, true).alive == 0,
            "no dust is counted with its layer off or at the slot's end");
    }

    {
        FlightBuilder builder("descent", 0, {0, 3 * R, 0}, {});
        builder.vertical_to(R, rotor::descent_rate);
        builder.hold(600);
        const auto plan = builder.build();
        const std::uint32_t seed = 26000;
        const std::uint32_t last = plan.knots.back().tick;
        check(last < RotorHistory::length, "the frame-split test's plan fits the history");
        std::vector<RotorStep> states(last + 1);
        RotorHistory in_order;
        for (std::uint32_t tick = 1; tick <= last; ++tick)
        {
            states[tick] = rotor_step(plan, tick, terrain, still, false, all_bits, reference);
            in_order.store(tick, states[tick], {});
        }
        RotorHistory split;
        std::uint32_t random = 1;
        std::uint32_t tick = 0;
        bool same = true;
        bool emitted = false;
        while (tick + 4 <= last)
        {
            const std::uint32_t steps = next_random(random) % 5;
            if (steps != 0 && next_random(random) % 8 == 0)
            {
                RotorStep other;
                other.emission = 1024;
                for (std::uint32_t step = 1; step <= steps; ++step) split.store(tick + step, other, {});
                continue;
            }
            for (std::uint32_t step = 1; step <= steps; ++step) split.store(tick + step, states[tick + step], {});
            tick += steps;
            const auto a = dust_census(split, seed, 0, never, tick, {}, true);
            const auto b = dust_census(in_order, seed, 0, never, tick, {}, true);
            same = same && a.alive == b.alive && a.mean_outer == b.mean_outer && a.mean_age == b.mean_age;
            emitted = emitted || a.alive > 100;
        }
        std::uint32_t replayed = 0;
        for (std::uint32_t local = rotor_layout::rotor_disc_records; local < particle_layout::explosion_particles; ++local)
        {
            const auto cycle = dust_cycle(seed, local);
            const std::uint32_t first = 1 + cycle.phase;
            if (tick < first) continue;
            const std::uint32_t index = (tick - first) / cycle.length;
            replayed += dust_born(seed, local, index, states[first + index * cycle.length].emission) ? 1 : 0;
        }
        check(same && emitted && replayed == dust_census(split, seed, 0, never, tick, {}, true).alive,
            "the census is the same for every split of ticks into frames, and as the birth rule replayed");
    }

    {
        FlightBuilder builder("approach", 1200, {-400, 60, -40}, {40, 0, 0});
        builder.cruise(227);
        builder.change_velocity({});
        builder.hold(300);
        builder.transit_to({-40, 50, -20});
        builder.vertical_to(12, rotor::descent_rate);
        builder.hold(300);
        builder.land(terrain, 300);
        builder.hold(1200);
        const auto plan = builder.build();
        bool continuous = true;
        bool limits = true;
        bool nose = true;
        for (std::size_t index = 1; index + 1 < plan.knots.size(); ++index)
        {
            const auto tick = plan.knots[index].tick;
            const auto before = flight_state(plan, tick - 1);
            const auto at = flight_state(plan, tick);
            const auto after = flight_state(plan, tick + 1);
            const double3 jump = (after.velocity - at.velocity) - (at.velocity - before.velocity);
            continuous = continuous && length(jump) < 0.01 && length(at.position - plan.knots[index].position) < 1e-6;
        }
        for (std::uint32_t tick = plan.list_tick; tick < plan.knots.back().tick; tick += 3)
        {
            const auto state = flight_state(plan, tick);
            const double horizontal = std::sqrt(state.velocity.x * state.velocity.x + state.velocity.z * state.velocity.z);
            limits = limits && horizontal <= rotor::cruise_speed + 1e-6 && length(state.acceleration) <= rotor::acceleration_limit + 1e-6 &&
                (horizontal >= 10 || state.velocity.y >= -rotor::descent_rate - 1e-6);
            if (horizontal > 5) nose = nose && std::abs(std::remainder(state.heading - std::atan2(state.velocity.z, state.velocity.x), 2 * rotor::pi)) < 0.35;
        }
        check(continuous, "flight plans are continuous at their knots");
        check(limits, "flight plans keep within 40 m/s, 3.5 m/s^2 and 3 m/s of slow descent");
        check(nose, "the helicopter flies nose first");
        check(plan.touchdown_tick != never && std::abs(flight_state(plan, plan.touchdown_tick).position.y - rotor::hub_above_skids) < 1e-9,
            "the skids touch the ground at touchdown");
        const auto landed = helicopter_pose(plan, plan.touchdown_tick + 10, terrain, still, false, all_bits);
        double worst_turn = 0;
        for (std::uint32_t tick = plan.touchdown_tick - 300; tick < plan.touchdown_tick + 60; ++tick)
        {
            const auto a = helicopter_pose(plan, tick, terrain, still, false, all_bits);
            const auto b = helicopter_pose(plan, tick + 1, terrain, still, false, all_bits);
            worst_turn = std::max(worst_turn, std::acos(std::clamp(dot(a.up, b.up), -1.0, 1.0)));
        }
        check(std::abs(landed.up.y - 1) < 1e-9 && worst_turn < 0.01, "the helicopter levels before touchdown, without a jump");
        check(helicopter_fade(plan, 1199) == 0 && helicopter_fade(plan, 1200) == 0 && helicopter_fade(plan, 1260) > 0 && helicopter_fade(plan, 1320) == 1,
            "the helicopter fades in over 120 ticks from listing");
        FlightBuilder away("depart", 0, {0, 30, 0}, {});
        away.depart({1, 0, 0}, 900);
        const auto departing = away.build();
        bool fading = true;
        for (std::uint32_t tick = 700; tick < 905; ++tick) fading = fading && helicopter_fade(departing, tick + 1) <= helicopter_fade(departing, tick);
        check(departing.unlist_tick == 900 && fading && helicopter_fade(departing, 900) == 0, "a departure dissolves and is unlisted");
        check(spin_share(plan, plan.spin_down_tick + 1200) == 0 && collective_share(plan, plan.touchdown_tick + 240) < 0.11, "the rotor idles and spins down");
    }

    if (failures == 0) std::puts("rotorcraft: all checks passed");
    return failures == 0 ? 0 : 1;
}
