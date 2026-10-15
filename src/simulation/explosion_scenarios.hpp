#pragma once

#include "environment.hpp"
#include "explosion_system.hpp"
#include "jet.hpp"
#include "rotorcraft.hpp"
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
    std::optional<FlightPlan> flight;
    std::optional<JetFlight> jet;
};

inline constexpr std::string_view scenario_names = "single, grid-10, grid-50, grid-100, barrage, presets, range, bombardment, accumulation, "
                                                   "linger-barrage, linger-saturation, heli-hover, heli-pass, heli-landing, jet-pass, jet-vapor";
constexpr bool is_helicopter_scenario(std::string_view name)
{
    return name == "heli-hover" || name == "heli-pass" || name == "heli-landing";
}
constexpr bool is_jet_scenario(std::string_view name)
{
    return name == "jet-pass" || name == "jet-vapor";
}
constexpr bool is_scenario_name(std::string_view name)
{
    return name == "single" || name == "grid-10" || name == "grid-50" || name == "grid-100" || name == "barrage" ||
        name == "presets" || name == "range" || name == "bombardment" || name == "accumulation" || name == "linger-barrage" ||
        name == "linger-saturation" || is_helicopter_scenario(name) || is_jet_scenario(name);
}

inline constexpr std::uint32_t jet_vapor_humidity_percent = 95;
JetFlight jet_flight(std::string_view name, const Environment& environment);

inline constexpr std::uint32_t helicopter_seed = 26000;
double helicopter_cruise_height(const Environment& environment, double ax, double az, double bx, double bz, double agl);

inline constexpr std::uint32_t linger_barrage_key = 14000;
inline constexpr std::uint32_t linger_saturation_key = 15000;

inline constexpr std::uint32_t accumulation_hold_ticks = 1800;
inline constexpr std::uint32_t accumulation_key = 12000;
inline constexpr std::uint32_t accumulation_first_seed = 5000;
std::uint32_t accumulation_interval(ExplosionPreset preset);
std::optional<Scenario> make_scenario(std::string_view name, const Environment& environment,
    ExplosionPreset preset = ExplosionPreset::small_he, std::uint32_t linger_ticks = 0);
}
