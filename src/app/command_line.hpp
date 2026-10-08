#pragma once

#include "range_view.hpp"
#include "../graphics/graphics_api.hpp"
#include "../simulation/explosion_presets.hpp"
#include "../simulation/wind.hpp"

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
    bool half_smoke = false;
    bool gpu_validation = false;
    std::optional<bool> ui;
    std::string adapter;
    std::uint32_t explosions = 1;
    std::optional<std::uint32_t> profile_tick;
    std::optional<std::uint32_t> exit_after;
    std::string capture_path;
    std::optional<std::uint32_t> capture_tick;
    RangeView view = RangeView::overview;
    SceneFormat scene_format = SceneFormat::rgba16f;
    std::string scenario;
    bool effect_lines = false;
    ExplosionPreset preset = ExplosionPreset::small_he;
    std::uint32_t layer_mask = particle_layout::full_mask;
    WindSettings wind;
};

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
