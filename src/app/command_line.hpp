#pragma once

#include <span>
#include <string>

namespace pyroclast
{
enum class Renderer
{
    d3d12,
    vulkan
};

struct ApplicationOptions
{
    Renderer renderer = Renderer::d3d12;
    bool show_help = false;
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
