#pragma once

#include "explosion_presets.hpp"
#include "explosion_system.hpp"
#include "../core/math.hpp"
#include "../graphics/realism_layout.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace pyroclast
{
double overpressure_ratio(double scaled_distance);
double front_speed(double overpressure);
double particle_velocity(double overpressure);

std::array<BlastKnot, realism_layout::blast_knots> make_blast_knots(double kilograms);
double integrated_arrival_ticks(double kilograms, double metres);

float blast_radius(const BlastParameters& blast, float ticks);
float blast_overpressure(const BlastParameters& blast, float metres);
std::uint32_t blast_arrival_tick(const BlastParameters& blast, std::uint32_t radius_cm);

float push_speed(const BlastParameters& blast, float metres);

struct BlastList
{
    std::uint32_t count = 0;
    std::array<std::uint32_t, realism_layout::maximum_blasts> slots{};
    std::array<float4, realism_layout::maximum_blasts> rings{};
    std::array<float4, realism_layout::maximum_blasts> fronts{};
    std::array<float4, realism_layout::maximum_blasts> returns{};
};
BlastList select_blasts(const ExplosionSystem& effects, std::span<const BlastParameters> classes, std::uint32_t tick_before, std::uint32_t tick_drawn,
    float humidity);
float wilson_opacity(const BlastParameters& blast, float ticks, float humidity);
struct WilsonShell
{
    float outer = 0;
    float inner = 0;
    float opacity = 0;
};
WilsonShell wilson_shell(const BlastParameters& blast, float ticks, float humidity);

float blast_arrival_ticks(const BlastParameters& blast, float metres);
float shake_angle(const ExplosionSystem& effects, std::span<const BlastParameters> classes, float3 camera, std::uint32_t tick);
}
