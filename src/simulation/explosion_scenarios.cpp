#include "explosion_scenarios.hpp"
#include "impact_patterns.hpp"
#include "../graphics/realism_layout.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <string>

namespace pyroclast
{
std::vector<ExplosionEvent> group_events(const Terrain& terrain, std::uint32_t count, ExplosionPreset preset)
{
    assert(is_explosion_scenario(count));
    const std::uint32_t columns = count == 1 ? 1 : count == 10 ? 5 : 10;
    const std::uint32_t rows = (count + columns - 1) / columns;
    std::vector<ExplosionEvent> events;
    events.reserve(count);
    for (std::uint32_t effect = 0; effect < count; ++effect)
    {
        const float x = (static_cast<float>(effect % columns) - 0.5f * static_cast<float>(columns - 1)) * group_spacing;
        const float z = (static_cast<float>(effect / columns) - 0.5f * static_cast<float>(rows - 1)) * group_spacing;
        events.push_back(ground_event(terrain, x, z, effect, preset));
    }
    return events;
}

namespace
{
std::uint32_t longest_life(ExplosionPreset preset)
{
    return std::max(effect_ticks(preset, SurfaceType::dirt), effect_ticks(preset, SurfaceType::concrete));
}

double highest_along(const Environment& environment, double ax, double az, double bx, double bz)
{
    const auto& terrain = environment.terrain();
    const double length = std::hypot(bx - ax, bz - az);
    const int samples = std::max(2, static_cast<int>(length / 2));
    double highest = -1e30;
    for (int index = 0; index <= samples; ++index)
    {
        const double t = static_cast<double>(index) / samples;
        highest = std::max(highest, static_cast<double>(terrain.height(static_cast<float>(ax + (bx - ax) * t), static_cast<float>(az + (bz - az) * t))));
    }
    for (const auto& box : environment.boxes())
    {
        const double reach = std::hypot(box.half_extent.x, box.half_extent.z) + rotor::radius + 2;
        const double dx = bx - ax;
        const double dz = bz - az;
        const double along = std::clamp(((box.centre.x - ax) * dx + (box.centre.z - az) * dz) / std::max(dx * dx + dz * dz, 1e-9), 0.0, 1.0);
        const double nearest = std::hypot(box.centre.x - (ax + dx * along), box.centre.z - (az + dz * along));
        if (nearest <= reach) highest = std::max(highest, static_cast<double>(box.centre.y + box.half_extent.y));
    }
    return highest;
}

double cruise_height(const Environment& environment, double ax, double az, double bx, double bz, double agl)
{
    return helicopter_cruise_height(environment, ax, az, bx, bz, agl);
}

FlightPlan helicopter_flight(std::string_view name, const Environment& environment)
{
    const auto& terrain = environment.terrain();
    const auto ground = [&terrain](double x, double z) { return static_cast<double>(terrain.height(static_cast<float>(x), static_cast<float>(z))); };
    if (name == "heli-hover")
    {
        const double hover = ground(0, -40) + 70;
        const double cruise = std::max(hover, cruise_height(environment, -400, -40, 0, -40, 60));
        FlightBuilder builder(std::string(name), 1200, {-400, cruise, -40}, {rotor::cruise_speed, 0, 0});
        builder.cruise(227);
        builder.change_velocity({});
        if (builder.position().y > hover) builder.vertical_to(hover, rotor::descent_rate);
        if (builder.tick() < 2900) builder.hold(2900 - builder.tick());
        builder.vertical_to(ground(0, -40) + rotor::radius, rotor::descent_rate);
        if (builder.tick() < 4940) builder.hold(4940 - builder.tick());
        builder.depart({1, 0, 0}, 900);
        return builder.build();
    }
    if (name == "heli-pass")
    {
        const double cruise = cruise_height(environment, -400, -30, 400, -30, 40);
        FlightBuilder builder(std::string(name), 1200, {-400, cruise, -30}, {rotor::cruise_speed, 0, 0});
        builder.cruise(90);
        builder.change_velocity({10, 0, 0});
        builder.cruise(1200);
        builder.depart({1, 0, 0}, 2400);
        auto plan = builder.build();
        std::uint32_t tick = plan.list_tick;
        while (tick < plan.unlist_tick && flight_state(plan, tick).position.x < 400) ++tick;
        plan.unlist_tick = tick;
        return plan;
    }
    const double hover = ground(-45, -20) + 30;
    const double cruise = std::max(hover, cruise_height(environment, -420, -20, -45, -20, 60));
    FlightBuilder builder(std::string(name), 300, {-420, cruise, -20}, {rotor::cruise_speed, 0, 0});
    builder.cruise(190);
    builder.change_velocity({});
    if (builder.position().y > hover) builder.vertical_to(hover, rotor::descent_rate);
    builder.hold(300);
    builder.land(terrain, 300);
    return builder.build();
}
}

double helicopter_cruise_height(const Environment& environment, double ax, double az, double bx, double bz, double agl)
{
    const auto& terrain = environment.terrain();
    double hub = agl + std::max(terrain.height(static_cast<float>(ax), static_cast<float>(az)), terrain.height(static_cast<float>(bx), static_cast<float>(bz)));
    const double needed = highest_along(environment, ax, az, bx, bz) + rotor::hub_above_skids + 2;
    for (int step = 0; step < 4 && hub < needed; ++step) hub += 10;
    return hub;
}

std::uint32_t accumulation_interval(ExplosionPreset preset)
{
    constexpr std::uint32_t slots = particle_layout::maximum_effects;
    return (longest_life(preset) + slots - 1) / slots;
}

std::optional<Scenario> make_scenario(std::string_view name, const Environment& environment, ExplosionPreset preset,
    std::uint32_t linger_ticks)
{
    if (!is_scenario_name(name)) return std::nullopt;
    const auto& terrain = environment.terrain();
    Scenario scenario{name, {}, 0};
    if (name == "range")
    {
        struct Aim
        {
            float3 origin;
            float3 direction;
            ExplosionPreset preset;
        };
        constexpr float3 down{0, -1, 0};
        const Aim aims[] = {
            {{-14, 20, -48.5f}, down, ExplosionPreset::artillery_he},
            {{-11, 1.5f, -40}, {0, 0, -1}, ExplosionPreset::small_impact},
            {{-2, 20, -50}, down, ExplosionPreset::small_he},
            {{8, 20, -45.5f}, down, ExplosionPreset::heavy},
            {{9.5f, 20, -55}, down, ExplosionPreset::small_he},
            {{-31.4f, 20, -45.7f}, down, ExplosionPreset::dust_blast},
            {{26.35f, 20, -47.93f}, down, ExplosionPreset::fuel_fire},
        };
        for (std::uint32_t index = 0; index < std::size(aims); ++index)
        {
            const auto& aim = aims[index];
            const auto hit = environment.raycast({aim.origin, aim.direction}, std::numeric_limits<float>::infinity(), true);
            assert(hit);
            if (!hit) continue;
            scenario.events.push_back({0, {environment.event_origin(*hit), hit->normal, 3000 + index, aim.preset, hit->surface}});
        }
    }
    else if (name == "presets")
    {
        for (std::uint32_t index = 0; index < preset_count; ++index)
        {
            const float x = (static_cast<float>(index) - 0.5f * static_cast<float>(preset_count - 1)) * 18.0f;
            scenario.events.push_back({0, ground_event(terrain, x, -30.0f, 2000 + index, static_cast<ExplosionPreset>(index))});
        }
    }
    else if (name == "bombardment")
    {
        constexpr std::uint32_t impacts = 400;
        constexpr std::uint32_t interval = 3;
        for (std::uint32_t index = 0; index < impacts; ++index)
        {
            const auto offset = bombardment_offset(8000, index, 30);
            scenario.events.push_back({index * interval, ground_event(terrain, offset.x, offset.y, 4000 + index, preset)});
        }
    }
    else if (name == "accumulation")
    {
        const auto interval = accumulation_interval(preset);
        const auto impacts = (longest_life(preset) + accumulation_hold_ticks + interval - 1) / interval;
        for (std::uint32_t index = 0; index < impacts; ++index)
        {
            const auto offset = bombardment_offset(accumulation_key, index, 30);
            scenario.events.push_back({index * interval, ground_event(terrain, offset.x, offset.y, accumulation_first_seed + index, preset)});
        }
    }
    else if (name == "linger-barrage" || is_helicopter_scenario(name))
    {
        const std::uint32_t impacts = name == "heli-pass" ? 160 : name == "heli-landing" ? 24 : 240;
        for (std::uint32_t index = 0; index < impacts; ++index)
        {
            const auto offset = bombardment_offset(linger_barrage_key, index, 30);
            scenario.events.push_back({index * 15, ground_event(terrain, offset.x, offset.y, 6000 + index, preset)});
        }
    }
    else if (name == "linger-saturation")
    {
        for (std::uint32_t index = 0; index < 1200; ++index)
        {
            const auto offset = bombardment_offset(linger_saturation_key, index, 30);
            const auto impact = index % 4 == 0 ? ExplosionPreset::heavy : ExplosionPreset::small_impact;
            scenario.events.push_back({index * 2, ground_event(terrain, offset.x, offset.y, 7000 + index, impact)});
        }
    }
    else if (name == "barrage")
    {
        for (std::uint32_t index = 0; index < barrage_impacts; ++index)
        {
            const auto offset = barrage_offset(0, index);
            scenario.events.push_back({index * barrage_interval, ground_event(terrain, offset.x, offset.y, 1000 + index, preset)});
        }
    }
    else
    {
        const std::uint32_t count = name == "single" ? 1 : name == "grid-10" ? 10 : name == "grid-50" ? 50 : 100;
        for (const auto& event : group_events(terrain, count, preset)) scenario.events.push_back({0, event});
    }
    if (is_helicopter_scenario(name)) scenario.flight = helicopter_flight(name, environment);
    if (name == "linger-barrage" || is_helicopter_scenario(name)) scenario.duration_ticks = 5400;
    else if (name == "linger-saturation") scenario.duration_ticks = 3000;
    else
    {
        for (const auto& scripted : scenario.events)
        {
            std::uint32_t ticks = effect_ticks(scripted.event.preset, scripted.event.surface);
            if (linger_ticks > 0)
            {
                ticks = std::max({ticks, realism_effect_ticks(scripted.event.preset, scripted.event.surface) + realism_layout::crossfade_ticks + 60,
                    linger_ticks * 1229 / 1024});
            }
            scenario.duration_ticks = std::max(scenario.duration_ticks, scripted.tick + ticks);
        }
    }
    return scenario;
}
}
