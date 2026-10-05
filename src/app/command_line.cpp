#include "command_line.hpp"

#include <string_view>

namespace pyroclast
{
CommandLineResult parse_command_line(std::span<const char* const> arguments)
{
    CommandLineResult result;
    bool renderer_specified = false;

    for (const char* argument : arguments)
    {
        const std::string_view option(argument);
        if (option == "--help")
        {
            result.options.show_help = true;
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
