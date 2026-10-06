#include "app/command_line.hpp"

#include <cstdio>
#include <initializer_list>
#include <string_view>

namespace
{
struct TestCase
{
    const char* name;
    std::initializer_list<const char*> arguments;
    pyroclast::Renderer renderer;
    bool show_help;
    std::string_view error_fragment;
    bool gpu_timing = false;
    std::uint32_t explosions = 1;
};
}

int main()
{
    using enum pyroclast::Renderer;
    const TestCase cases[] = {
        {"ten explosions", {"--explosions=10"}, d3d12, false, {}, false, 10},
        {"fifty explosions", {"--explosions=50"}, d3d12, false, {}, false, 50},
        {"hundred Vulkan explosions", {"--renderer=vulkan", "--explosions=100", "--gpu-timing"}, vulkan, false, {}, true, 100},
        {"unsupported count", {"--explosions=2"}, d3d12, false, "Expected --explosions"},
        {"unbounded count", {"--explosions=1000000"}, d3d12, false, "Expected --explosions"},
        {"empty count", {"--explosions="}, d3d12, false, "Expected --explosions"},
        {"duplicate count", {"--explosions=10", "--explosions=50"}, d3d12, false, "only once"},
        {"help validates count", {"--help", "--explosions=-1"}, d3d12, false, "Expected --explosions"},
        {"GPU timing", {"--gpu-timing"}, d3d12, false, {}, true},
        {"Vulkan GPU timing", {"--renderer=vulkan", "--gpu-timing"}, vulkan, false, {}, true},
        {"invalid GPU timing value", {"--gpu-timing=true"}, d3d12, false, "Unknown argument"},
        {"default renderer", {}, d3d12, false, {}},
        {"explicit d3d12", {"--renderer=d3d12"}, d3d12, false, {}},
        {"explicit vulkan", {"--renderer=vulkan"}, vulkan, false, {}},
        {"help", {"--help"}, d3d12, true, {}},
        {"renderer before help", {"--renderer=vulkan", "--help"}, vulkan, true, {}},
        {"help before renderer", {"--help", "--renderer=vulkan"}, vulkan, true, {}},
        {"repeated help", {"--help", "--help"}, d3d12, true, {}},
        {"unknown renderer", {"--renderer=opengl"}, d3d12, false, "Invalid renderer 'opengl'"},
        {"empty renderer", {"--renderer="}, d3d12, false, "Expected --renderer=d3d12 or --renderer=vulkan"},
        {"case sensitive renderer", {"--renderer=Vulkan"}, d3d12, false, "Invalid renderer 'Vulkan'"},
        {"missing renderer value", {"--renderer"}, d3d12, false, "Unknown argument '--renderer'"},
        {"separate renderer value", {"--renderer", "vulkan"}, d3d12, false, "Unknown argument '--renderer'"},
        {"unknown option", {"--unknown"}, d3d12, false, "Unknown argument '--unknown'"},
        {"positional argument", {"vulkan"}, d3d12, false, "Unknown argument 'vulkan'"},
        {"empty argument", {""}, d3d12, false, "Unknown argument ''"},
        {"trailing renderer characters", {"--renderer=vulkan-extra"}, d3d12, false, "Invalid renderer 'vulkan-extra'"},
        {"duplicate renderer", {"--renderer=d3d12", "--renderer=d3d12"}, d3d12, false, "only once"},
        {"conflicting renderers", {"--renderer=d3d12", "--renderer=vulkan"}, d3d12, false, "only once"},
        {"help does not hide invalid options", {"--help", "--unknown"}, d3d12, false, "Unknown argument '--unknown'"},
        {"invalid options before help", {"--unknown", "--help"}, d3d12, false, "Unknown argument '--unknown'"}
    };

    int failures = 0;
    for (const auto& test : cases)
    {
        const auto result = pyroclast::parse_command_line(
            {test.arguments.begin(), test.arguments.size()});
        const bool passed = test.error_fragment.empty()
            ? result.error.empty() && result.options.renderer == test.renderer &&
                result.options.show_help == test.show_help && result.options.gpu_timing == test.gpu_timing && result.options.explosions == test.explosions
            : result.error.find(test.error_fragment) != std::string::npos;
        if (!passed)
        {
            std::fprintf(stderr, "FAIL: %s (error: %s)\n", test.name, result.error.c_str());
            ++failures;
        }
    }

    if (std::string_view(pyroclast::renderer_name(d3d12)) != "d3d12" ||
        std::string_view(pyroclast::renderer_name(vulkan)) != "vulkan")
    {
        std::fputs("FAIL: renderer names\n", stderr);
        ++failures;
    }

    return failures == 0 ? 0 : 1;
}
