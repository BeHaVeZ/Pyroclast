#include "command_line.hpp"

#include "../simulation/explosion_scenarios.hpp"

#include <charconv>
#include <optional>
#include <string_view>

namespace pyroclast
{
namespace
{
std::optional<std::uint32_t> parse_count(std::string_view value, std::uint32_t minimum, std::uint32_t maximum)
{
    std::uint32_t parsed_value = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), parsed_value);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        parsed_value < minimum || parsed_value > maximum)
    {
        return std::nullopt;
    }
    return parsed_value;
}
}

CommandLineResult parse_command_line(std::span<const char* const> arguments)
{
    CommandLineResult result;
    bool renderer_specified = false;
    bool explosions_specified = false;
    bool profile_specified = false;
    bool smoke_specified = false;
    bool capture_specified = false;

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
        else if (option == "--gpu-validation")
        {
            result.options.gpu_validation = true;
        }
        else if (option.starts_with("--ui="))
        {
            if (result.options.ui)
            {
                result.error = "Specify --ui only once.";
                return result;
            }
            const auto value = option.substr(std::string_view("--ui=").size());
            if (value != "on" && value != "off")
            {
                result.error = "Expected --ui=on or off.";
                return result;
            }
            result.options.ui = value == "on";
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
            result.options.profile_tick = parse_count(option.substr(std::string_view("--profile-tick=").size()), 0, 480);
            if (!result.options.profile_tick)
            {
                result.error = "Expected --profile-tick=N with an integer from 0 to 480.";
                return result;
            }
        }
        else if (option.starts_with("--exit-after="))
        {
            if (result.options.exit_after)
            {
                result.error = "Specify --exit-after only once.";
                return result;
            }
            result.options.exit_after = parse_count(option.substr(std::string_view("--exit-after=").size()), 1, 10'000'000);
            if (!result.options.exit_after)
            {
                result.error = "Expected --exit-after=N with an integer from 1 to 10000000.";
                return result;
            }
        }
        else if (option.starts_with("--capture="))
        {
            if (capture_specified)
            {
                result.error = "Specify --capture only once.";
                return result;
            }
            capture_specified = true;
            result.options.capture_path = option.substr(std::string_view("--capture=").size());
            if (result.options.capture_path.empty())
            {
                result.error = "Expected --capture=<file.png>.";
                return result;
            }
        }
        else if (option.starts_with("--explosions="))
        {
            if (explosions_specified)
            {
                result.error = "Specify --explosions only once.";
                return result;
            }
            explosions_specified = true;
            const auto explosions = parse_count(option.substr(std::string_view("--explosions=").size()), 1, maximum_explosions);
            if (!explosions || !is_explosion_scenario(*explosions))
            {
                result.error = "Expected --explosions=1, 10, 50, or 100.";
                return result;
            }
            result.options.explosions = *explosions;
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

    if (capture_specified && !result.options.profile_tick)
    {
        result.error = "--capture requires --profile-tick so the captured state is reproducible.";
    }
    return result;
}

const char* renderer_name(Renderer renderer)
{
    return renderer == Renderer::d3d12 ? "d3d12" : "vulkan";
}
}
