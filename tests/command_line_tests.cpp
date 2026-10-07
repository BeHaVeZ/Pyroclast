#include "app/command_line.hpp"
#include "graphics/adapter_name.hpp"

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
    std::optional<std::uint32_t> profile_tick;
    bool half_smoke = false;
    std::optional<std::uint32_t> exit_after;
    std::string_view capture_path;
    bool gpu_validation = false;
    std::optional<bool> ui;
    bool gpu_regions = false;
    std::string_view adapter;
};
}

int main()
{
    using enum pyroclast::Renderer;
    const TestCase cases[] = {
        {"adapter by name", {"--adapter=UHD"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, {}, false, "UHD"},
        {"WARP adapter", {"--adapter=warp"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, {}, false, "warp"},
        {"empty adapter", {"--adapter="}, d3d12, false, "Expected --adapter"},
        {"duplicate adapter", {"--adapter=a", "--adapter=b"}, d3d12, false, "only once"},
        {"GPU regions", {"--gpu-regions"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, {}, true},
        {"overlay on", {"--ui=on"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, true},
        {"overlay off", {"--ui=off"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, false},
        {"overlay invalid", {"--ui=yes"}, d3d12, false, "Expected --ui"},
        {"overlay duplicate", {"--ui=on", "--ui=off"}, d3d12, false, "only once"},
        {"GPU validation", {"--gpu-validation"}, d3d12, false, {}, false, 1, {}, false, {}, {}, true},
        {"GPU validation with a value", {"--gpu-validation=1"}, d3d12, false, "Unknown argument"},
        {"exit after", {"--exit-after=600"}, d3d12, false, {}, false, 1, {}, false, 600},
        {"exit after zero", {"--exit-after=0"}, d3d12, false, "Expected --exit-after"},
        {"exit after empty", {"--exit-after="}, d3d12, false, "Expected --exit-after"},
        {"exit after too large", {"--exit-after=10000001"}, d3d12, false, "Expected --exit-after"},
        {"exit after duplicate", {"--exit-after=1", "--exit-after=2"}, d3d12, false, "only once"},
        {"capture at profile tick", {"--profile-tick=180", "--capture=out/a.png"}, d3d12, false, {}, false, 1, 180, false, {}, "out/a.png"},
        {"capture before profile tick", {"--capture=a.png", "--profile-tick=0"}, d3d12, false, {}, false, 1, 0, false, {}, "a.png"},
        {"capture without profile tick", {"--capture=a.png"}, d3d12, false, "--capture requires --profile-tick"},
        {"capture empty path", {"--capture=", "--profile-tick=0"}, d3d12, false, "Expected --capture"},
        {"capture duplicate", {"--profile-tick=0", "--capture=a.png", "--capture=b.png"}, d3d12, false, "only once"},
        {"half smoke", {"--smoke-resolution=half"}, d3d12, false, {}, false, 1, {}, true},
        {"full smoke", {"--smoke-resolution=full"}, d3d12, false, {}},
        {"invalid smoke resolution", {"--smoke-resolution=quarter"}, d3d12, false, "Expected --smoke-resolution"},
        {"duplicate smoke resolution", {"--smoke-resolution=full", "--smoke-resolution=half"}, d3d12, false, "only once"},
        {"profile tick zero", {"--profile-tick=0"}, d3d12, false, {}, false, 1, 0},
        {"profile tick death", {"--profile-tick=480"}, d3d12, false, {}, false, 1, 480},
        {"profile tick overflow", {"--profile-tick=4294967296"}, d3d12, false, "Expected --profile-tick"},
        {"profile tick out of range", {"--profile-tick=481"}, d3d12, false, "Expected --profile-tick"},
        {"profile tick negative", {"--profile-tick=-1"}, d3d12, false, "Expected --profile-tick"},
        {"profile tick empty", {"--profile-tick="}, d3d12, false, "Expected --profile-tick"},
        {"profile tick trailing text", {"--profile-tick=180x"}, d3d12, false, "Expected --profile-tick"},
        {"profile tick duplicate", {"--profile-tick=180", "--profile-tick=60"}, d3d12, false, "only once"},
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
                result.options.show_help == test.show_help && result.options.gpu_timing == test.gpu_timing && result.options.explosions == test.explosions && result.options.profile_tick == test.profile_tick && result.options.half_smoke == test.half_smoke &&
                result.options.exit_after == test.exit_after && result.options.capture_path == test.capture_path &&
                result.options.gpu_validation == test.gpu_validation && result.options.ui == test.ui &&
                result.options.gpu_regions == test.gpu_regions && result.options.adapter == test.adapter
            : result.error.find(test.error_fragment) != std::string::npos;
        if (!passed)
        {
            std::fprintf(stderr, "FAIL: %s (error: %s)\n", test.name, result.error.c_str());
            ++failures;
        }
    }

    if (!pyroclast::adapter_matches("Intel(R) UHD Graphics 770", "uhd") || !pyroclast::adapter_matches("NVIDIA GeForce RTX 4090", "RTX 4090") ||
        pyroclast::adapter_matches("NVIDIA GeForce RTX 4090", "radeon") || !pyroclast::adapter_matches("Microsoft Basic Render Driver", ""))
    {
        std::fputs("FAIL: adapter name matching\n", stderr);
        ++failures;
    }
    if (std::string_view(pyroclast::renderer_name(d3d12)) != "d3d12" ||
        std::string_view(pyroclast::renderer_name(vulkan)) != "vulkan")
    {
        std::fputs("FAIL: renderer names\n", stderr);
        ++failures;
    }

    return failures == 0 ? 0 : 1;
}
