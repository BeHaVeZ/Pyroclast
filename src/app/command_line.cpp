#include "command_line.hpp"

#include "../simulation/explosion_scenarios.hpp"

#include <charconv>
#include <optional>
#include <string>
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
    bool scene_format_specified = false;
    bool capture_specified = false;
    bool view_specified = false;
    const auto set_once = [&result](bool& flag, const char* name) {
        if (flag)
        {
            result.error = std::string("Specify ") + name + " only once.";
            return false;
        }
        flag = true;
        return true;
    };

    for (const char* argument : arguments)
    {
        const std::string_view option(argument);
        if (option == "--help")
        {
            result.options.show_help = true;
        }
        else if (option == "--gpu-timing")
        {
            if (!set_once(result.options.gpu_timing, "--gpu-timing")) return result;
        }
        else if (option == "--gpu-regions")
        {
            if (!set_once(result.options.gpu_regions, "--gpu-regions")) return result;
        }
        else if (option == "--gpu-validation")
        {
            if (!set_once(result.options.gpu_validation, "--gpu-validation")) return result;
        }
        else if (option.starts_with("--adapter="))
        {
            if (!result.options.adapter.empty())
            {
                result.error = "Specify --adapter only once.";
                return result;
            }
            result.options.adapter = option.substr(std::string_view("--adapter=").size());
            if (result.options.adapter.empty())
            {
                result.error = "Expected --adapter=<part of the GPU name> or --adapter=warp.";
                return result;
            }
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
        else if (option.starts_with("--view="))
        {
            if (view_specified)
            {
                result.error = "Specify --view only once.";
                return result;
            }
            view_specified = true;
            const auto value = option.substr(std::string_view("--view=").size());
            if (value == "overview") result.options.view = RangeView::overview;
            else if (value == "ground") result.options.view = RangeView::ground;
            else if (value == "hill") result.options.view = RangeView::hill;
            else if (value == "aerial") result.options.view = RangeView::aerial;
            else
            {
                result.error = "Expected --view=overview, ground, hill or aerial.";
                return result;
            }
        }
        else if (option.starts_with("--scene-format="))
        {
            if (scene_format_specified)
            {
                result.error = "Specify --scene-format only once.";
                return result;
            }
            scene_format_specified = true;
            const auto value = option.substr(std::string_view("--scene-format=").size());
            if (value == "rgba16f") result.options.scene_format = SceneFormat::rgba16f;
            else if (value == "r11g11b10f") result.options.scene_format = SceneFormat::r11g11b10f;
            else if (value == "rgba8") result.options.scene_format = SceneFormat::rgba8;
            else
            {
                result.error = "Expected --scene-format=rgba16f, r11g11b10f or rgba8.";
                return result;
            }
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

const char* scene_format_name(SceneFormat format)
{
    switch (format)
    {
    case SceneFormat::rgba16f: return "rgba16f";
    case SceneFormat::r11g11b10f: return "r11g11b10f";
    case SceneFormat::rgba8: return "rgba8";
    }
    return "rgba16f";
}

const char* view_name(RangeView view)
{
    switch (view)
    {
    case RangeView::overview: return "overview";
    case RangeView::ground: return "ground";
    case RangeView::hill: return "hill";
    case RangeView::aerial: return "aerial";
    }
    return "overview";
}
}
