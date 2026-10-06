#include "command_line.hpp"

#include "../simulation/explosion_scenarios.hpp"

#include <charconv>
#include <string_view>

namespace pyroclast
{
CommandLineResult parse_command_line(std::span<const char* const> arguments)
{
    CommandLineResult result;
    bool renderer_specified = false;
    bool explosions_specified = false;
    bool profile_specified = false;
    bool smoke_specified = false;

    for (const char* argument : arguments)
    {
        const std::string_view option(argument);
        if (option == "--help")
        {
            result.options.show_help = true;
        }
        else if (option == "--gpu-timing")
        {
            result.options.gpu_timing = true;
        }
        else if (option.starts_with("--smoke-resolution="))
        {
            if (smoke_specified)
            {
                result.error = "Specify --smoke-resolution only once.";
                return result;
            }
            smoke_specified = true;
            const auto value = option.substr(std::string_view("--smoke-resolution=").size());
            if (value == "half") result.options.half_smoke = true;
            else if (value != "full")
            {
                result.error = "Expected --smoke-resolution=full or half.";
                return result;
            }
        }
        else if (option.starts_with("--profile-tick="))
        {
            if (profile_specified)
            {
                result.error = "Specify --profile-tick only once.";
                return result;
            }
            profile_specified = true;
            const auto value = option.substr(std::string_view("--profile-tick=").size());
            std::uint32_t tick = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), tick);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || tick > 480)
            {
                result.error = "Expected --profile-tick=N with an integer from 0 to 480.";
                return result;
            }
            result.options.profile_tick = tick;
        }
        else if (option.starts_with("--explosions="))
        {
            if (explosions_specified)
            {
                result.error = "Specify --explosions only once.";
                return result;
            }
            explosions_specified = true;
            const auto value = option.substr(std::string_view("--explosions=").size());
            std::uint32_t explosions = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), explosions);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || !is_explosion_scenario(explosions))
            {
                result.error = "Expected --explosions=1, 10, 50, or 100.";
                return result;
            }
            result.options.explosions = explosions;
        }
        else if (option.starts_with("--renderer="))
        {
            if (renderer_specified)
            {
                result.error = "Specify --renderer only once.";
                return result;
            }
            renderer_specified = true;

            const auto value = option.substr(std::string_view("--renderer=").size());
            if (value == "d3d12")
            {
                result.options.renderer = Renderer::d3d12;
            }
            else if (value == "vulkan")
            {
                result.options.renderer = Renderer::vulkan;
            }
            else
            {
                result.error = "Invalid renderer '" + std::string(value) +
                    "'. Expected --renderer=d3d12 or --renderer=vulkan.";
                return result;
            }
        }
        else
        {
            result.error = "Unknown argument '" + std::string(option) +
                "'. Use --help for usage.";
            return result;
        }
    }

    return result;
}

const char* renderer_name(Renderer renderer)
{
    return renderer == Renderer::d3d12 ? "d3d12" : "vulkan";
}
}
