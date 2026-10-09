#pragma once

#include "environment.hpp"
#include "explosion_system.hpp"
#include "terrain.hpp"
#include "../graphics/particle_layout.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace pyroclast
{
constexpr std::uint32_t maximum_explosions = particle_layout::maximum_explosions;

constexpr bool is_explosion_scenario(std::uint32_t explosions)
{
    return explosions == 1 || explosions == 10 || explosions == 50 || explosions == maximum_explosions;
}

inline constexpr float group_spacing = 9;

std::vector<ExplosionEvent> group_events(const Terrain& terrain, std::uint32_t count, ExplosionPreset preset = ExplosionPreset::small_he);

struct ScriptedEvent
{
    std::uint32_t tick;
    ExplosionEvent event;
};

struct Scenario
{
    std::string_view name;
    std::vector<ScriptedEvent> events;
    std::uint32_t duration_ticks = 0;
};

inline constexpr std::string_view scenario_names =
    "single, grid-10, grid-50, grid-100, barrage, presets, range, bombardment, accumulation, linger-barrage, linger-saturation";
constexpr bool is_scenario_name(std::string_view name)
{
    return name == "single" || name == "grid-10" || name == "grid-50" || name == "grid-100" || name == "barrage" ||
        name == "presets" || name == "range" || name == "bombardment" || name == "accumulation" || name == "linger-barrage" ||
        name == "linger-saturation";
}

inline constexpr std::uint32_t linger_barrage_key = 14000;
inline constexpr std::uint32_t linger_saturation_key = 15000;

inline constexpr std::uint32_t accumulation_hold_ticks = 1800;
inline constexpr std::uint32_t accumulation_key = 12000;
inline constexpr std::uint32_t accumulation_first_seed = 5000;
std::uint32_t accumulation_interval(ExplosionPreset preset);
std::optional<Scenario> make_scenario(std::string_view name, const Environment& environment,
    ExplosionPreset preset = ExplosionPreset::small_he, std::uint32_t linger_ticks = 0);
}
