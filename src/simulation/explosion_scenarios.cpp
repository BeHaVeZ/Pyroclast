#include "explosion_scenarios.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <limits>

namespace pyroclast
{
namespace
{
ExplosionEvent on_ground(const Terrain& terrain, float x, float z, std::uint32_t seed, ExplosionPreset preset)
{
    const auto normal = terrain.normal(x, z);
    return {float3{x, terrain.height(x, z), z} + normal * 0.5f, normal, seed, preset, Terrain::surface(x, z)};
}

float jitter(std::uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return static_cast<float>(value & 0xffffu) / 32768.0f - 1.0f;
}
}

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
        events.push_back(on_ground(terrain, x, z, effect, preset));
    }
    return events;
}

std::optional<Scenario> make_scenario(std::string_view name, const Environment& environment, ExplosionPreset preset)
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
            scenario.events.push_back({0, on_ground(terrain, x, -30.0f, 2000 + index, static_cast<ExplosionPreset>(index))});
        }
    }
    else if (name == "barrage")
    {
        constexpr std::uint32_t impacts = 80;
        constexpr std::uint32_t interval = 15;
        for (std::uint32_t index = 0; index < impacts; ++index)
        {
            const float x = -60.0f + 120.0f * static_cast<float>(index) / static_cast<float>(impacts - 1) + 4.0f * jitter(2 * index);
            const float z = 12.0f * jitter(2 * index + 1);
            scenario.events.push_back({index * interval, on_ground(terrain, x, z, 1000 + index, preset)});
        }
    }
    else
    {
        const std::uint32_t count = name == "single" ? 1 : name == "grid-10" ? 10 : name == "grid-50" ? 50 : 100;
        for (const auto& event : group_events(terrain, count, preset)) scenario.events.push_back({0, event});
    }
    for (const auto& scripted : scenario.events)
    {
        scenario.duration_ticks = std::max(scenario.duration_ticks,
            scripted.tick + effect_ticks(scripted.event.preset, scripted.event.surface));
    }
    return scenario;
}
}
