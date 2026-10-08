#include "app/command_line.hpp"
#include "graphics/adapter_name.hpp"

#include <cmath>
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
    pyroclast::RangeView view = pyroclast::RangeView::overview;
    pyroclast::SceneFormat scene_format = pyroclast::SceneFormat::rgba16f;
    std::string_view scenario;
    bool effect_lines = false;
    pyroclast::ExplosionPreset preset = pyroclast::ExplosionPreset::small_he;
    std::uint32_t layer_mask = pyroclast::particle_layout::full_mask;
    pyroclast::WindSettings wind;
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
        {"ground view", {"--view=ground"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, {}, false, {}, pyroclast::RangeView::ground},
        {"aerial view", {"--view=aerial"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, {}, false, {}, pyroclast::RangeView::aerial},
        {"range view", {"--view=range"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, {}, false, {}, pyroclast::RangeView::range},
        {"yard view", {"--view=yard"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, {}, false, {}, pyroclast::RangeView::yard},
        {"invalid view", {"--view=top"}, d3d12, false, "Expected --view"},
        {"packed float scene", {"--scene-format=r11g11b10f"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, {}, false, {}, {},
            pyroclast::SceneFormat::r11g11b10f},
        {"8-bit scene", {"--scene-format=rgba8"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, {}, false, {}, {},
            pyroclast::SceneFormat::rgba8},
        {"invalid scene format", {"--scene-format=rgba32f"}, d3d12, false, "Expected --scene-format"},
        {"barrage scenario", {"--scenario=barrage"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, {}, false, {}, {},
            pyroclast::SceneFormat::rgba16f, "barrage"},
        {"invalid scenario", {"--scenario=storm"}, d3d12, false, "Expected --scenario=single, grid-10, grid-50, grid-100, barrage, presets, range, bombardment or accumulation"},
        {"presets scenario", {"--scenario=presets"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, {}, false, {}, {}, {}, "presets"},
        {"preset", {"--preset=fuel-fire"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, {}, false, {}, {}, {}, {}, false,
            pyroclast::ExplosionPreset::fuel_fire},
        {"invalid preset", {"--preset=nuke"}, d3d12, false, "Expected --preset=small-impact, small-he, artillery-he, heavy, fuel-fire, dust-blast or stress-test"},
        {"duplicate preset", {"--preset=heavy", "--preset=heavy"}, d3d12, false, "only once"},
        {"disable layers", {"--disable=debris,lights,smoke"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, {}, false, {}, {}, {}, {},
            false, pyroclast::ExplosionPreset::small_he, pyroclast::particle_layout::full_mask & ~((1u << 6) | (1u << 7) | (1u << 1))},
        {"disable smoke features", {"--disable=curl,vortex,gusts,shading,shadow,emission"}, d3d12, false, {}, false, 1, {}, false, {}, {},
            false, {}, false, {}, {}, {}, {}, false, pyroclast::ExplosionPreset::small_he, pyroclast::particle_layout::full_mask & ~0x3f00u},
        {"disable props", {"--disable=props"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, {}, false, {}, {}, {}, {},
            false, pyroclast::ExplosionPreset::small_he, pyroclast::particle_layout::full_mask & ~(1u << 14)},
        {"disable collision", {"--disable=collision"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, {}, false, {}, {}, {}, {},
            false, pyroclast::ExplosionPreset::small_he, pyroclast::particle_layout::full_mask & ~(1u << 15)},
        {"disable the props' interaction", {"--disable=props,collision,deflection"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false,
            {}, false, {}, {}, {}, {}, false, pyroclast::ExplosionPreset::small_he, 0x3fffu},
        {"wind", {"--wind=6.5,-45,0.8"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, {}, false, {}, {}, {}, {}, false,
            pyroclast::ExplosionPreset::small_he, pyroclast::particle_layout::full_mask, {6.5f, -45.0f, 0.8f}},
        {"wind too strong", {"--wind=41,0,0"}, d3d12, false, "Expected --wind"},
        {"wind too gusty", {"--wind=5,0,1.5"}, d3d12, false, "Expected --wind"},
        {"wind with two values", {"--wind=5,0"}, d3d12, false, "Expected --wind"},
        {"wind with four values", {"--wind=5,0,0,1"}, d3d12, false, "Expected --wind"},
        {"wind not a number", {"--wind=fast,0,0"}, d3d12, false, "Expected --wind"},
        {"wind heading wraps", {"--wind=2,450,0"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, {}, false, {}, {}, {}, {}, false,
            pyroclast::ExplosionPreset::small_he, pyroclast::particle_layout::full_mask, {2.0f, 90.0f, 0.0f}},
        {"huge wind heading wraps", {"--wind=2,3e38,0"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, {}, false, {}, {}, {}, {}, false,
            pyroclast::ExplosionPreset::small_he, pyroclast::particle_layout::full_mask,
            {2.0f, std::remainder(3e38f, 360.0f), 0.0f}},
        {"infinite wind", {"--wind=inf,0,0"}, d3d12, false, "Expected --wind"},
        {"duplicate wind", {"--wind=1,0,0", "--wind=1,0,0"}, d3d12, false, "only once"},
        {"disable an unknown layer", {"--disable=smoke,clouds"}, d3d12, false, "Expected --disable"},
        {"disable nothing", {"--disable="}, d3d12, false, "Expected --disable"},
        {"disable with a trailing comma", {"--disable=smoke,"}, d3d12, false, "Expected --disable"},
        {"duplicate scenario", {"--scenario=single", "--scenario=single"}, d3d12, false, "only once"},
        {"duplicate effect lines", {"--effect-lines", "--effect-lines"}, d3d12, false, "only once"},
        {"effect lines", {"--effect-lines"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, {}, false, {}, {},
            pyroclast::SceneFormat::rgba16f, {}, true},
        {"scenario with a group", {"--scenario=single", "--explosions=10"}, d3d12, false, "cannot be combined"},
        {"frozen scenario", {"--profile-tick=10", "--scenario=single"}, d3d12, false, {}, false, 1, 10, false, {}, {}, false, {}, false, {},
            {}, {}, "single"},
        {"frozen scenario past the startup range", {"--scenario=bombardment", "--profile-tick=1500"}, d3d12, false, {}, false, 1, 1500,
            false, {}, {}, false, {}, false, {}, {}, {}, "bombardment"},
        {"frozen scenario at tick 0", {"--scenario=single", "--profile-tick=0"}, d3d12, false, "starts at 1"},
        {"frozen scenario with a capture tick", {"--scenario=single", "--profile-tick=10", "--capture=a.png", "--capture-tick=5"}, d3d12,
            false, "cannot be combined with --profile-tick"},
        {"duplicate scene format", {"--scene-format=rgba8", "--scene-format=rgba8"}, d3d12, false, "only once"},
        {"duplicate view", {"--view=hill", "--view=hill"}, d3d12, false, "only once"},
        {"GPU regions duplicate", {"--gpu-regions", "--gpu-regions"}, d3d12, false, "only once"},
        {"GPU timing duplicate", {"--gpu-timing", "--gpu-timing"}, d3d12, false, "only once"},
        {"GPU validation duplicate", {"--gpu-validation", "--gpu-validation"}, d3d12, false, "only once"},
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
        {"budget without auto", {"--smoke-budget=coverage:300"}, d3d12, false, "--smoke-budget requires --smoke-resolution=auto"},
        {"budget of another kind", {"--smoke-resolution=auto", "--smoke-budget=pixels:300"}, d3d12, false, "Expected --smoke-budget"},
        {"budget not positive", {"--smoke-resolution=auto", "--smoke-budget=count:0"}, d3d12, false, "Expected --smoke-budget"},
        {"budget twice", {"--smoke-resolution=auto", "--smoke-budget=count:5", "--smoke-budget=count:6"}, d3d12, false, "only once"},
        {"half light at full resolution", {"--half-light"}, d3d12, false, "--half-light requires"},
        {"thinning radius zero", {"--smoke-lod=0"}, d3d12, false, "Expected --smoke-lod"},
        {"thinning radius text", {"--smoke-lod=wide"}, d3d12, false, "Expected --smoke-lod"},
        {"faint floor at one", {"--faint-floor=1"}, d3d12, false, "Expected --faint-floor"},
        {"faint floor twice", {"--faint-floor=0.001", "--faint-floor=0.002"}, d3d12, false, "only once"},
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
        {"invalid options before help", {"--unknown", "--help"}, d3d12, false, "Unknown argument '--unknown'"},
        {"range scenario", {"--scenario=range"}, d3d12, false, {}, false, 1, {}, false, {}, {}, false, {}, false, {}, {},
            pyroclast::SceneFormat::rgba16f, "range"},
        {"capture tick without a scenario", {"--profile-tick=10", "--capture=a.png", "--capture-tick=5"}, d3d12, false,
            "--capture-tick requires --scenario and --capture"},
        {"capture tick without a capture", {"--scenario=range", "--capture-tick=5"}, d3d12, false,
            "--capture-tick requires --scenario and --capture"},
        {"scenario capture without a tick", {"--scenario=range", "--capture=a.png"}, d3d12, false, "--capture requires --profile-tick"},
        {"capture tick zero", {"--scenario=range", "--capture=a.png", "--capture-tick=0"}, d3d12, false, "Expected --capture-tick"},
        {"capture tick too late", {"--scenario=range", "--capture=a.png", "--capture-tick=100001"}, d3d12, false, "Expected --capture-tick"},
        {"capture tick duplicate", {"--scenario=range", "--capture=a.png", "--capture-tick=5", "--capture-tick=6"}, d3d12, false, "only once"}
    };

    int failures = 0;
    for (const auto& test : cases)
    {
        const auto result = pyroclast::parse_command_line(
            {test.arguments.begin(), test.arguments.size()});
        const bool passed = test.error_fragment.empty()
            ? result.error.empty() && result.options.renderer == test.renderer &&
                result.options.show_help == test.show_help && result.options.gpu_timing == test.gpu_timing && result.options.explosions == test.explosions && result.options.profile_tick == test.profile_tick && (result.options.scalability.smoke_resolution == pyroclast::SmokeResolution::half) == test.half_smoke &&
                result.options.exit_after == test.exit_after && result.options.capture_path == test.capture_path &&
                result.options.gpu_validation == test.gpu_validation && result.options.ui == test.ui &&
                result.options.gpu_regions == test.gpu_regions && result.options.adapter == test.adapter &&
                result.options.view == test.view && result.options.scene_format == test.scene_format &&
                result.options.scenario == test.scenario && result.options.effect_lines == test.effect_lines &&
                result.options.preset == test.preset && result.options.layer_mask == test.layer_mask &&
                result.options.wind.speed == test.wind.speed && result.options.wind.direction_degrees == test.wind.direction_degrees &&
                result.options.wind.gustiness == test.wind.gustiness
            : result.error.find(test.error_fragment) != std::string::npos;
        if (!passed)
        {
            std::fprintf(stderr, "FAIL: %s (error: %s)\n", test.name, result.error.c_str());
            ++failures;
        }
    }

    const char* views_list[] = {"--debug-views=regions,wind"};
    const auto with_views = pyroclast::parse_command_line(views_list);
    const char* views_all[] = {"--debug-views=all"};
    const auto all_views = pyroclast::parse_command_line(views_all);
    if (!with_views.error.empty() || with_views.options.debug_views.effect_bounds || with_views.options.debug_views.collision_boxes ||
        !with_views.options.debug_views.spawn_regions || !with_views.options.debug_views.wind_arrow || !all_views.error.empty() ||
        !all_views.options.debug_views.effect_bounds || !all_views.options.debug_views.collision_boxes ||
        !all_views.options.debug_views.spawn_regions || !all_views.options.debug_views.wind_arrow)
    {
        std::fputs("FAIL: --debug-views lists and all\n", stderr);
        ++failures;
    }
    const char* views_unknown[] = {"--debug-views=regions,grid"};
    const char* views_twice[] = {"--debug-views=wind", "--debug-views=bounds"};
    const char* views_lines[] = {"--debug-views=wind", "--effect-lines"};
    const char* views_scenario[] = {"--debug-views=wind", "--scenario=range"};
    if (pyroclast::parse_command_line(views_unknown).error.find("Expected --debug-views") == std::string::npos ||
        pyroclast::parse_command_line(views_twice).error.find("only once") == std::string::npos ||
        pyroclast::parse_command_line(views_lines).error.find("not both") == std::string::npos ||
        pyroclast::parse_command_line(views_scenario).error.find("--debug-views cannot be combined") == std::string::npos)
    {
        std::fputs("FAIL: --debug-views errors\n", stderr);
        ++failures;
    }

    const char* scenario_capture[] = {"--scenario=range", "--capture=r.png", "--capture-tick=45"};
    const auto captured = pyroclast::parse_command_line(scenario_capture);
    if (!captured.error.empty() || captured.options.capture_tick != 45u || captured.options.capture_path != "r.png" ||
        captured.options.scenario != "range" || captured.options.profile_tick)
    {
        std::fprintf(stderr, "FAIL: a scenario capture at a tick (error: %s)\n", captured.error.c_str());
        ++failures;
    }
    if (pyroclast::parse_command_line({}).options.capture_tick)
    {
        std::fputs("FAIL: no capture tick by default\n", stderr);
        ++failures;
    }

    const char* camera[] = {"--camera=0,18,78,-78,6,78.5"};
    const auto posed = pyroclast::parse_command_line(camera);
    if (!posed.error.empty() || !posed.options.camera || (*posed.options.camera)[0] != 0 || (*posed.options.camera)[1] != 18 ||
        (*posed.options.camera)[3] != -78 || (*posed.options.camera)[5] != 78.5f || pyroclast::parse_command_line({}).options.camera)
    {
        std::fprintf(stderr, "FAIL: --camera (error: %s)\n", posed.error.c_str());
        ++failures;
    }
    const char* camera_short[] = {"--camera=0,18,78,0,6"};
    const char* camera_long[] = {"--camera=0,18,78,0,6,0,1"};
    const char* camera_text[] = {"--camera=0,18,78,0,6,x"};
    const char* camera_same[] = {"--camera=1,2,3,1,2,3"};
    const char* camera_infinite[] = {"--camera=0,18,inf,0,6,0"};
    const char* camera_view[] = {"--camera=0,18,78,0,6,0", "--view=ground"};
    const char* camera_twice[] = {"--camera=0,18,78,0,6,0", "--camera=0,18,78,0,6,1"};
    if (pyroclast::parse_command_line(camera_short).error.find("Expected --camera") == std::string::npos ||
        pyroclast::parse_command_line(camera_long).error.find("Expected --camera") == std::string::npos ||
        pyroclast::parse_command_line(camera_text).error.find("Expected --camera") == std::string::npos ||
        pyroclast::parse_command_line(camera_same).error.find("Expected --camera") == std::string::npos ||
        pyroclast::parse_command_line(camera_infinite).error.find("Expected --camera") == std::string::npos ||
        pyroclast::parse_command_line(camera_view).error.find("not both") == std::string::npos ||
        pyroclast::parse_command_line(camera_twice).error.find("only once") == std::string::npos)
    {
        std::fputs("FAIL: --camera errors\n", stderr);
        ++failures;
    }

    const char* automatic[] = {"--smoke-resolution=auto"};
    const char* covered_budget[] = {"--smoke-resolution=auto", "--smoke-budget=coverage:250.5"};
    const char* counted_budget[] = {"--smoke-budget=count:60000", "--smoke-resolution=auto"};
    const auto automatic_options = pyroclast::parse_command_line(automatic).options.scalability;
    const auto covered_options = pyroclast::parse_command_line(covered_budget).options.scalability;
    const auto counted_options = pyroclast::parse_command_line(counted_budget).options.scalability;
    if (automatic_options.smoke_resolution != pyroclast::SmokeResolution::automatic || automatic_options.smoke_budget.limit != 0 ||
        covered_options.smoke_budget.count || covered_options.smoke_budget.limit != 250.5 || !counted_options.smoke_budget.count ||
        counted_options.smoke_budget.limit != 60000 || pyroclast::parse_command_line({}).options.scalability.smoke_resolution !=
        pyroclast::SmokeResolution::full)
    {
        std::fputs("FAIL: --smoke-resolution=auto and --smoke-budget\n", stderr);
        ++failures;
    }

    const char* lod[] = {"--smoke-lod=32", "--faint-floor=0.0025"};
    const auto lod_options = pyroclast::parse_command_line(lod).options.scalability;
    if (lod_options.smoke_lod != 32 || lod_options.faint_floor != 0.0025f || pyroclast::parse_command_line({}).options.scalability.smoke_lod != 0)
    {
        std::fputs("FAIL: --smoke-lod and --faint-floor\n", stderr);
        ++failures;
    }

    const char* cloud[] = {"--effect-order=cloud"};
    const char* unordered[] = {"--effect-order=none"};
    const char* misordered[] = {"--effect-order=depth"};
    if (pyroclast::parse_command_line(cloud).options.scalability.effect_order != pyroclast::EffectOrder::cloud ||
        pyroclast::parse_command_line(unordered).options.scalability.effect_order != pyroclast::EffectOrder::none ||
        pyroclast::parse_command_line({}).options.scalability.effect_order != pyroclast::EffectOrder::origin ||
        pyroclast::parse_command_line(misordered).error.find("Expected --effect-order") == std::string::npos)
    {
        std::fputs("FAIL: --effect-order\n", stderr);
        ++failures;
    }

    const char* evicting[] = {"--slot-policy=evict-oldest"};
    const char* bad_policy[] = {"--slot-policy=random"};
    if (pyroclast::parse_command_line(evicting).options.scalability.slot_policy != pyroclast::SlotPolicy::evict_oldest ||
        pyroclast::parse_command_line({}).options.scalability.slot_policy != pyroclast::SlotPolicy::drop_new ||
        pyroclast::parse_command_line(bad_policy).error.find("Expected --slot-policy") == std::string::npos)
    {
        std::fputs("FAIL: --slot-policy\n", stderr);
        ++failures;
    }

    const char* covered[] = {"--coverage"};
    const char* covered_twice[] = {"--coverage", "--coverage"};
    if (pyroclast::parse_command_line({}).options.scalability.coverage || !pyroclast::parse_command_line(covered).options.scalability.coverage ||
        pyroclast::parse_command_line(covered_twice).error.find("only once") == std::string::npos)
    {
        std::fputs("FAIL: --coverage\n", stderr);
        ++failures;
    }

    const char* logged[] = {"--scenario=grid-100", "--counter-log=out/a.csv"};
    const char* logged_profile[] = {"--profile-tick=180", "--counter-log=b.csv"};
    const char* logged_interactive[] = {"--counter-log=c.csv"};
    const char* logged_empty[] = {"--scenario=single", "--counter-log="};
    const char* logged_twice[] = {"--scenario=single", "--counter-log=a.csv", "--counter-log=b.csv"};
    if (pyroclast::parse_command_line(logged).options.counter_log != "out/a.csv" ||
        pyroclast::parse_command_line(logged_profile).options.counter_log != "b.csv" ||
        !pyroclast::parse_command_line(logged_profile).error.empty() ||
        pyroclast::parse_command_line(logged_interactive).error.find("--counter-log requires") == std::string::npos ||
        pyroclast::parse_command_line(logged_empty).error.find("Expected --counter-log") == std::string::npos ||
        pyroclast::parse_command_line(logged_twice).error.find("only once") == std::string::npos)
    {
        std::fputs("FAIL: --counter-log\n", stderr);
        ++failures;
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
