#pragma once

#include "../graphics/graphics_api.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace pyroclast
{
struct ApplicationOptions
{
    Renderer renderer = Renderer::d3d12;
    bool show_help = false;
    bool gpu_timing = false;
    bool half_smoke = false;
    std::uint32_t explosions = 1;
    std::optional<std::uint32_t> profile_tick;
};

struct CommandLineResult
{
    ApplicationOptions options;
    std::string error;
};

// Arguments exclude the executable name.
CommandLineResult parse_command_line(std::span<const char* const> arguments);
const char* renderer_name(Renderer renderer);
}
