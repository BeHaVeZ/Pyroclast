#include "explosion_scenarios.hpp"

#include <array>
#include <cassert>

namespace pyroclast
{
namespace
{
ExplosionEvent on_ground(const Terrain& terrain, float x, float z, std::uint32_t seed)
{
    const auto normal = terrain.normal(x, z);
    return {float3{x, terrain.height(x, z), z} + normal * 0.5f, normal, seed};
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

std::vector<ExplosionEvent> group_events(const Terrain& terrain, std::uint32_t count)
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
        events.push_back(on_ground(terrain, x, z, effect));
    }
    return events;
}

std::optional<Scenario> make_scenario(std::string_view name, const Terrain& terrain)
{
    if (!is_scenario_name(name)) return std::nullopt;
    Scenario scenario{name, {}, 0};
    if (name == "barrage")
    {
        constexpr std::uint32_t impacts = 80;
        constexpr std::uint32_t interval = 15;
        for (std::uint32_t index = 0; index < impacts; ++index)
        {
            const float x = -60.0f + 120.0f * static_cast<float>(index) / static_cast<float>(impacts - 1) + 4.0f * jitter(2 * index);
            const float z = 12.0f * jitter(2 * index + 1);
            scenario.events.push_back({index * interval, on_ground(terrain, x, z, 1000 + index)});
        }
        scenario.duration_ticks = (impacts - 1) * interval + ExplosionSystem::effect_ticks;
        return scenario;
    }
    const std::uint32_t count = name == "single" ? 1 : name == "grid-10" ? 10 : name == "grid-50" ? 50 : 100;
    for (const auto& event : group_events(terrain, count)) scenario.events.push_back({0, event});
    scenario.duration_ticks = ExplosionSystem::effect_ticks;
    return scenario;
}
}
