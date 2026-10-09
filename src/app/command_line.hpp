#pragma once

#include "debug_views.hpp"
#include "range_view.hpp"
#include "scalability.hpp"
#include "../graphics/graphics_api.hpp"
#include "../graphics/realism_layout.hpp"
#include "../simulation/explosion_presets.hpp"
#include "../simulation/wind.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace pyroclast
{
struct WindowSize
{
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    bool operator==(const WindowSize&) const = default;
};

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
    std::string capture_sequence;
    std::optional<std::uint32_t> capture_from;
    std::optional<std::uint32_t> capture_to;
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
    WindowSize window;
    bool borderless = false;
    std::uint32_t linger_seconds = 75;
    std::uint32_t humidity_percent = 80;
};

struct BenchmarkWorkload
{
    std::string_view name;
    std::string_view scenario;
    ExplosionPreset preset;
    RangeView view;
    WindowSize window{};
    std::uint32_t mask = particle_layout::full_mask;
    WindSettings wind{};
    bool borderless = false;
};
inline constexpr WindowSize realism_benchmark_window{1920, 1080};
inline constexpr std::array<BenchmarkWorkload, 10> benchmark_workloads{{
    {"single", "single", ExplosionPreset::small_he, RangeView::overview},
    {"simultaneous-10", "grid-10", ExplosionPreset::small_he, RangeView::overview},
    {"simultaneous-50", "grid-50", ExplosionPreset::small_he, RangeView::overview},
    {"simultaneous-100", "grid-100", ExplosionPreset::small_he, RangeView::overview},
    {"sustained-barrage", "barrage", ExplosionPreset::artillery_he, RangeView::overview},
    {"smoke-field", "grid-100", ExplosionPreset::heavy, RangeView::ground},
    {"accumulation", "accumulation", ExplosionPreset::artillery_he, RangeView::overview},
    {"linger-barrage", "linger-barrage", ExplosionPreset::artillery_he, RangeView::overview, realism_benchmark_window,
        realism_layout::realism_full_mask, {2, 270, 0.3f}, true},
    {"linger-far", "linger-barrage", ExplosionPreset::artillery_he, RangeView::far, realism_benchmark_window,
        realism_layout::realism_full_mask, {2, 300, 0.3f}, true},
    {"linger-ground", "linger-barrage", ExplosionPreset::artillery_he, RangeView::ground, realism_benchmark_window,
        realism_layout::realism_full_mask, {2, 270, 0.3f}, true}}};
inline constexpr std::string_view benchmark_names =
    "single, simultaneous-10, simultaneous-50, simultaneous-100, sustained-barrage, smoke-field, accumulation, linger-barrage, linger-far, "
    "linger-ground";
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
inline constexpr WindowSize minimum_window{640, 360};
inline constexpr WindowSize maximum_window{7680, 4320};
const char* view_name(RangeView view);
const char* scene_format_name(SceneFormat format);
inline constexpr std::string_view disable_names = "dust, smoke, plume, flash, fireball, sparks, debris, lights, curl, vortex, gusts, "
                                                  "shading, shadow, emission, props, collision, deflection, linger, rise, shear, "
                                                  "variation, haze, waves, skirt, condensation, push, shake";
std::optional<std::uint32_t> disable_bit(std::string_view name);
}
