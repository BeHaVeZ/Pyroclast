#pragma once

#include "debug_views.hpp"
#include "range_view.hpp"
#include "scalability.hpp"
#include "../graphics/graphics_api.hpp"
#include "../simulation/explosion_presets.hpp"
#include "../simulation/wind.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace pyroclast
{
enum class SceneFormat { rgba16f, r11g11b10f, rgba8 };

struct ApplicationOptions
{
    Renderer renderer = Renderer::d3d12;
    bool show_help = false;
    bool gpu_timing = false;
    bool gpu_regions = false;
    bool gpu_validation = false;
    std::optional<bool> ui;
    std::string adapter;
    std::uint32_t explosions = 1;
    std::optional<std::uint32_t> profile_tick;
    std::optional<std::uint32_t> exit_after;
    std::string capture_path;
    std::optional<std::uint32_t> capture_tick;
    RangeView view = RangeView::overview;
    std::optional<std::array<float, 6>> camera;
    SceneFormat scene_format = SceneFormat::rgba16f;
    std::string scenario;
    bool effect_lines = false;
    DebugViews debug_views;
    ExplosionPreset preset = ExplosionPreset::small_he;
    std::uint32_t layer_mask = particle_layout::full_mask;
    WindSettings wind;
    std::string counter_log;
    ScalabilitySettings scalability;
    std::string benchmark;
};

struct BenchmarkWorkload
{
    std::string_view name;
    std::string_view scenario;
    ExplosionPreset preset;
    RangeView view;
};
inline constexpr std::array<BenchmarkWorkload, 7> benchmark_workloads{{
    {"single", "single", ExplosionPreset::small_he, RangeView::overview},
    {"simultaneous-10", "grid-10", ExplosionPreset::small_he, RangeView::overview},
    {"simultaneous-50", "grid-50", ExplosionPreset::small_he, RangeView::overview},
    {"simultaneous-100", "grid-100", ExplosionPreset::small_he, RangeView::overview},
    {"sustained-barrage", "barrage", ExplosionPreset::artillery_he, RangeView::overview},
    {"smoke-field", "grid-100", ExplosionPreset::heavy, RangeView::ground},
    {"accumulation", "accumulation", ExplosionPreset::artillery_he, RangeView::overview}}};
inline constexpr std::string_view benchmark_names =
    "single, simultaneous-10, simultaneous-50, simultaneous-100, sustained-barrage, smoke-field, accumulation";
inline constexpr std::uint32_t benchmark_warmup_frames = 120;
constexpr const BenchmarkWorkload* benchmark_workload(std::string_view name)
{
    for (const auto& workload : benchmark_workloads)
    {
        if (workload.name == name) return &workload;
    }
    return nullptr;
}

inline constexpr std::uint32_t maximum_profile_tick = 480;

struct CommandLineResult
{
    ApplicationOptions options;
    std::string error;
};

// Arguments exclude the executable name.
CommandLineResult parse_command_line(std::span<const char* const> arguments);
const char* renderer_name(Renderer renderer);
const char* view_name(RangeView view);
const char* scene_format_name(SceneFormat format);
inline constexpr std::string_view disable_names = "dust, smoke, plume, flash, fireball, sparks, debris, lights, curl, vortex, gusts, "
                                                  "shading, shadow, emission, props, collision, deflection";
std::optional<std::uint32_t> disable_bit(std::string_view name);
}
