#include "command_line.hpp"

#include "../simulation/explosion_scenarios.hpp"

#include <array>
#include <bitset>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

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

bool parse_numbers(std::string_view values, std::span<float> parsed)
{
    std::size_t start = 0;
    for (std::size_t index = 0; index < parsed.size(); ++index)
    {
        const auto comma = values.find(',', start);
        const auto text = values.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed[index]);
        if (text.empty() || error != std::errc() || end != text.data() + text.size() || !std::isfinite(parsed[index]) ||
            (index + 1 == parsed.size()) != (comma == std::string_view::npos))
        {
            return false;
        }
        start = comma + 1;
    }
    return true;
}
}

std::optional<std::uint32_t> disable_bit(std::string_view name)
{
    for (std::uint32_t layer = 0; layer < particle_layout::layer_count; ++layer)
    {
        if (layer_name(static_cast<EffectLayer>(layer)) == name) return layer;
    }
    constexpr std::pair<std::string_view, std::uint32_t> others[] = {{"lights", particle_layout::lights_mask_bit},
        {"curl", particle_layout::curl_mask_bit}, {"vortex", particle_layout::vortex_mask_bit}, {"gusts", particle_layout::gusts_mask_bit},
        {"shading", particle_layout::shading_mask_bit}, {"shadow", particle_layout::shadow_mask_bit},
        {"emission", particle_layout::emission_mask_bit}, {"props", particle_layout::props_mask_bit},
        {"collision", particle_layout::collision_mask_bit}, {"deflection", particle_layout::deflection_mask_bit},
        {"linger", realism_layout::linger_mask_bit}, {"rise", realism_layout::rise_mask_bit}, {"shear", realism_layout::shear_mask_bit},
        {"variation", realism_layout::variation_mask_bit}, {"haze", realism_layout::haze_mask_bit}, {"waves", realism_layout::waves_mask_bit},
        {"skirt", realism_layout::skirt_mask_bit}, {"condensation", realism_layout::condensation_mask_bit}, {"push", realism_layout::push_mask_bit},
        {"shake", realism_layout::shake_mask_bit}, {"rotor", realism_layout::rotor_mask_bit}, {"brownout", realism_layout::brownout_mask_bit}};
    for (const auto& [other, bit] : others)
    {
        if (other == name) return bit;
    }
    return std::nullopt;
}

namespace
{
std::string or_list(std::string_view names)
{
    std::string text(names);
    const auto last = text.rfind(", ");
    if (last != std::string::npos) text.replace(last, 2, " or ");
    return text;
}
}

namespace
{
struct ParseState
{
    ApplicationOptions& options;
    std::optional<bool> realism;
    std::uint32_t disabled = 0;
};

using FlagHandler = std::string (*)(std::string_view value, ParseState& state);

struct Flag
{
    std::string_view name;
    bool takes_value;
    FlagHandler apply;
};

std::string ok()
{
    return {};
}

template <typename Take>
bool each_listed(std::string_view list, Take&& take)
{
    for (std::size_t start = 0;;)
    {
        const auto comma = list.find(',', start);
        if (!take(list.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start))) return false;
        if (comma == std::string_view::npos) return true;
        start = comma + 1;
    }
}

std::string parse_window(std::string_view value, ParseState& state)
{
    const auto separator = value.find('x');
    const auto width = separator == std::string_view::npos ? std::nullopt :
        parse_count(value.substr(0, separator), minimum_window.width, maximum_window.width);
    const auto height = separator == std::string_view::npos ? std::nullopt :
        parse_count(value.substr(separator + 1), minimum_window.height, maximum_window.height);
    if (!width || !height) return "Expected --window=WxH in pixels, from 640x360 to 7680x4320.";
    state.options.window = {*width, *height};
    return ok();
}

std::string parse_debug_views(std::string_view list, ParseState& state)
{
    auto& views = state.options.debug_views;
    const bool listed = each_listed(list, [&views](std::string_view name) {
        if (name == "bounds") views.effect_bounds = true;
        else if (name == "boxes") views.collision_boxes = true;
        else if (name == "regions") views.spawn_regions = true;
        else if (name == "wind") views.wind_arrow = true;
        else if (name == "all") views = {true, true, true, true};
        else return false;
        return true;
    });
    return listed ? ok() : "Expected --debug-views=bounds, boxes, regions, wind or all, comma-separated.";
}

std::string parse_adapter(std::string_view value, ParseState& state)
{
    state.options.adapter = value;
    return value.empty() ? "Expected --adapter=<part of the GPU name> or --adapter=warp." : ok();
}

std::string parse_realism(std::string_view value, ParseState& state)
{
    if (value != "on" && value != "off") return "Expected --realism=on or off.";
    state.realism = value == "on";
    return ok();
}

std::string parse_linger(std::string_view value, ParseState& state)
{
    const auto seconds = parse_count(value, 10, 300);
    if (!seconds) return "Expected --linger=S, seconds from 10 to 300.";
    state.options.linger_seconds = *seconds;
    return ok();
}

std::string parse_humidity(std::string_view value, ParseState& state)
{
    const auto percent = parse_count(value, 0, 100);
    if (!percent) return "Expected --humidity=P, relative humidity in percent from 0 to 100.";
    state.options.humidity_percent = *percent;
    return ok();
}

std::string parse_ui(std::string_view value, ParseState& state)
{
    if (value != "on" && value != "off") return "Expected --ui=on or off.";
    state.options.ui = value == "on";
    return ok();
}

std::string parse_view(std::string_view value, ParseState& state)
{
    for (std::uint32_t index = 0; index <= static_cast<std::uint32_t>(RangeView::far); ++index)
    {
        if (value == view_name(static_cast<RangeView>(index)))
        {
            state.options.view = static_cast<RangeView>(index);
            return ok();
        }
    }
    return "Expected --view=overview, ground, hill, aerial, range, yard or far.";
}

std::string parse_camera(std::string_view value, ParseState& state)
{
    std::array<float, 6> pose{};
    const bool parsed = parse_numbers(value, pose);
    const float x = pose[3] - pose[0];
    const float y = pose[4] - pose[1];
    const float z = pose[5] - pose[2];
    if (!parsed || x * x + y * y + z * z < 1e-4f) return "Expected --camera=EX,EY,EZ,TX,TY,TZ: an eye and a different target in metres.";
    state.options.camera = pose;
    return ok();
}

std::string parse_counter_log(std::string_view value, ParseState& state)
{
    state.options.counter_log = value;
    return value.empty() ? "Expected --counter-log=<file.csv>." : ok();
}

std::string parse_benchmark(std::string_view value, ParseState& state)
{
    if (!benchmark_workload(value)) return "Expected --benchmark=" + or_list(benchmark_names) + ".";
    state.options.benchmark = std::string(value);
    return ok();
}

std::string parse_scenario(std::string_view value, ParseState& state)
{
    state.options.scenario = value;
    return is_scenario_name(state.options.scenario) ? ok() : "Expected --scenario=" + or_list(scenario_names) + ".";
}

std::string parse_preset(std::string_view value, ParseState& state)
{
    const auto preset = preset_from_name(value);
    if (!preset) return "Expected --preset=" + or_list(preset_names) + ".";
    state.options.preset = *preset;
    return ok();
}

std::string parse_wind(std::string_view value, ParseState& state)
{
    float parsed[3]{};
    const auto wind = parse_numbers(value, parsed) ? validated({parsed[0], parsed[1], parsed[2]}) : std::nullopt;
    if (!wind) return "Expected --wind=SPEED,DIRECTION,GUSTINESS (0-40 m/s, degrees, 0-1).";
    state.options.wind = *wind;
    return ok();
}

std::string parse_disable(std::string_view list, ParseState& state)
{
    const bool listed = each_listed(list, [&state](std::string_view name) {
        const auto bit = disable_bit(name);
        if (bit) state.disabled |= 1u << *bit;
        return bit.has_value();
    });
    return listed ? ok() : "Expected --disable=<comma-separated " + or_list(disable_names) + ">.";
}

std::string parse_scene_format(std::string_view value, ParseState& state)
{
    for (const auto format : {SceneFormat::rgba16f, SceneFormat::r11g11b10f, SceneFormat::rgba8})
    {
        if (value == scene_format_name(format))
        {
            state.options.scene_format = format;
            return ok();
        }
    }
    return "Expected --scene-format=rgba16f, r11g11b10f or rgba8.";
}

std::string parse_smoke_resolution(std::string_view value, ParseState& state)
{
    auto& resolution = state.options.scalability.smoke_resolution;
    if (value == "half") resolution = SmokeResolution::half;
    else if (value == "auto") resolution = SmokeResolution::automatic;
    else if (value != "full") return "Expected --smoke-resolution=full, half or auto.";
    return ok();
}

std::string parse_smoke_lod(std::string_view value, ParseState& state)
{
    float pixels[1]{};
    if (!parse_numbers(value, pixels) || pixels[0] <= 0 || pixels[0] > 4096) return "Expected --smoke-lod=P, a radius in pixels above 0 and at most 4096.";
    state.options.scalability.smoke_lod = pixels[0];
    return ok();
}

std::string parse_faint_floor(std::string_view value, ParseState& state)
{
    float opacity[1]{};
    if (!parse_numbers(value, opacity) || opacity[0] <= 0 || opacity[0] >= 1) return "Expected --faint-floor=F, an opacity between 0 and 1.";
    state.options.scalability.faint_floor = opacity[0];
    return ok();
}

std::string parse_slot_policy(std::string_view value, ParseState& state)
{
    auto& policy = state.options.scalability.slot_policy;
    if (value == "evict-oldest") policy = SlotPolicy::evict_oldest;
    else if (value == "graduate-oldest") policy = SlotPolicy::graduate_oldest;
    else if (value != "drop-new") return "Expected --slot-policy=drop-new, evict-oldest or graduate-oldest.";
    return ok();
}

std::string parse_effect_order(std::string_view value, ParseState& state)
{
    auto& order = state.options.scalability.effect_order;
    if (value == "none") order = EffectOrder::none;
    else if (value == "cloud") order = EffectOrder::cloud;
    else if (value != "origin") return "Expected --effect-order=origin, none or cloud.";
    return ok();
}

std::string parse_smoke_budget(std::string_view value, ParseState& state)
{
    const bool count = value.starts_with("count:");
    float limit[1]{};
    const auto number = value.substr(value.find(':') == std::string_view::npos ? value.size() : value.find(':') + 1);
    if ((!count && !value.starts_with("coverage:")) || !parse_numbers(number, limit) || limit[0] <= 0)
    {
        return "Expected --smoke-budget=coverage:SCREENS or count:PARTICLES, a positive number.";
    }
    state.options.scalability.smoke_budget = {count, limit[0]};
    return ok();
}

std::string parse_profile_tick(std::string_view value, ParseState& state)
{
    state.options.profile_tick = parse_count(value, 0, 100'000);
    return state.options.profile_tick ? ok() : "Expected --profile-tick=N with an integer from 0 to 480 (1 up to the scenario's duration with --scenario).";
}

std::string parse_exit_after(std::string_view value, ParseState& state)
{
    state.options.exit_after = parse_count(value, 1, 10'000'000);
    return state.options.exit_after ? ok() : "Expected --exit-after=N with an integer from 1 to 10000000.";
}

std::string parse_capture_sequence(std::string_view value, ParseState& state)
{
    state.options.capture_sequence = value;
    return value.empty() ? "Expected --capture-sequence=<folder>." : ok();
}

std::string parse_capture_from(std::string_view value, ParseState& state)
{
    state.options.capture_from = parse_count(value, 1, 100'000);
    return state.options.capture_from ? ok() : "Expected --capture-from=T with an integer from 1 to 100000.";
}

std::string parse_capture_to(std::string_view value, ParseState& state)
{
    state.options.capture_to = parse_count(value, 1, 100'000);
    return state.options.capture_to ? ok() : "Expected --capture-to=T with an integer from 1 to 100000.";
}

std::string parse_capture_tick(std::string_view value, ParseState& state)
{
    state.options.capture_tick = parse_count(value, 1, 100'000);
    return state.options.capture_tick ? ok() : "Expected --capture-tick=N with an integer from 1 to 100000.";
}

std::string parse_capture(std::string_view value, ParseState& state)
{
    state.options.capture_path = value;
    return value.empty() ? "Expected --capture=<file.png>." : ok();
}

std::string parse_explosions(std::string_view value, ParseState& state)
{
    const auto explosions = parse_count(value, 1, maximum_explosions);
    if (!explosions || !is_explosion_scenario(*explosions)) return "Expected --explosions=1, 10, 50, or 100.";
    state.options.explosions = *explosions;
    return ok();
}

std::string parse_renderer(std::string_view value, ParseState& state)
{
    if (value == "d3d12") state.options.renderer = Renderer::d3d12;
    else if (value == "vulkan") state.options.renderer = Renderer::vulkan;
    else return "Invalid renderer '" + std::string(value) + "'. Expected --renderer=d3d12 or --renderer=vulkan.";
    return ok();
}

template <bool ApplicationOptions::*Member>
std::string set_switch(std::string_view, ParseState& state)
{
    state.options.*Member = true;
    return ok();
}

std::string set_half_light(std::string_view, ParseState& state)
{
    state.options.scalability.half_light = true;
    return ok();
}

std::string set_coverage(std::string_view, ParseState& state)
{
    state.options.scalability.coverage = true;
    return ok();
}

std::string set_help(std::string_view, ParseState& state)
{
    state.options.show_help = true;
    return ok();
}

constexpr Flag flags[] = {
    {"--help", false, set_help},
    {"--gpu-timing", false, set_switch<&ApplicationOptions::gpu_timing>},
    {"--gpu-regions", false, set_switch<&ApplicationOptions::gpu_regions>},
    {"--half-light", false, set_half_light},
    {"--coverage", false, set_coverage},
    {"--borderless", false, set_switch<&ApplicationOptions::borderless>},
    {"--effect-lines", false, set_switch<&ApplicationOptions::effect_lines>},
    {"--gpu-validation", false, set_switch<&ApplicationOptions::gpu_validation>},
    {"--window", true, parse_window},
    {"--debug-views", true, parse_debug_views},
    {"--adapter", true, parse_adapter},
    {"--realism", true, parse_realism},
    {"--linger", true, parse_linger},
    {"--humidity", true, parse_humidity},
    {"--ui", true, parse_ui},
    {"--view", true, parse_view},
    {"--camera", true, parse_camera},
    {"--counter-log", true, parse_counter_log},
    {"--benchmark", true, parse_benchmark},
    {"--scenario", true, parse_scenario},
    {"--preset", true, parse_preset},
    {"--wind", true, parse_wind},
    {"--disable", true, parse_disable},
    {"--scene-format", true, parse_scene_format},
    {"--smoke-resolution", true, parse_smoke_resolution},
    {"--smoke-lod", true, parse_smoke_lod},
    {"--faint-floor", true, parse_faint_floor},
    {"--slot-policy", true, parse_slot_policy},
    {"--effect-order", true, parse_effect_order},
    {"--smoke-budget", true, parse_smoke_budget},
    {"--profile-tick", true, parse_profile_tick},
    {"--exit-after", true, parse_exit_after},
    {"--capture-sequence", true, parse_capture_sequence},
    {"--capture-from", true, parse_capture_from},
    {"--capture-to", true, parse_capture_to},
    {"--capture-tick", true, parse_capture_tick},
    {"--capture", true, parse_capture},
    {"--explosions", true, parse_explosions},
    {"--renderer", true, parse_renderer},
};

const Flag* find_flag(std::string_view argument)
{
    const auto equals = argument.find('=');
    const auto name = argument.substr(0, equals);
    for (const auto& flag : flags)
    {
        if (flag.name == name && flag.takes_value == (equals != std::string_view::npos)) return &flag;
    }
    return nullptr;
}
}

CommandLineResult parse_command_line(std::span<const char* const> arguments)
{
    CommandLineResult result;
    ParseState state{result.options};
    std::bitset<std::size(flags)> given;
    const auto seen = [&given](std::string_view name) {
        for (std::size_t index = 0; index < std::size(flags); ++index)
        {
            if (flags[index].name == name) return given.test(index);
        }
        return false;
    };
    for (const char* argument : arguments)
    {
        const std::string_view option(argument);
        const auto* flag = find_flag(option);
        if (!flag)
        {
            result.error = "Unknown argument '" + std::string(option) + "'. Use --help for usage.";
            return result;
        }
        const auto index = static_cast<std::size_t>(flag - std::begin(flags));
        if (given.test(index) && flag->name != "--help")
        {
            result.error = "Specify " + std::string(flag->name) + " only once.";
            return result;
        }
        given.set(index);
        const auto value = flag->takes_value ? option.substr(flag->name.size() + 1) : std::string_view();
        if (auto error = flag->apply(value, state); !error.empty())
        {
            result.error = std::move(error);
            return result;
        }
    }
    const auto& realism = state.realism;
    const auto disabled = state.disabled;
    bool scenario_specified = seen("--scenario");
    const bool explosions_specified = seen("--explosions");
    const bool preset_specified = seen("--preset");
    const bool view_specified = seen("--view");
    const bool profile_specified = seen("--profile-tick");
    const bool debug_views_specified = seen("--debug-views");
    const bool window_specified = seen("--window");
    const bool wind_specified = seen("--wind");
    const bool linger_specified = seen("--linger");
    const bool humidity_specified = seen("--humidity");
    const bool policy_specified = seen("--slot-policy");
    const bool capture_specified = seen("--capture");
    const bool budget_specified = seen("--smoke-budget");

    std::uint32_t model = realism.value_or(true) ? realism_layout::realism_full_mask : particle_layout::full_mask;
    if (const auto workload = benchmark_workload(result.options.benchmark))
    {
        const char* conflict = scenario_specified ? "--scenario" : explosions_specified ? "--explosions" : preset_specified ? "--preset" :
            view_specified ? "--view" : result.options.camera ? "--camera" : profile_specified ? "--profile-tick" :
            result.options.exit_after ? "--exit-after" : result.options.ui.value_or(false) ? "--ui=on" :
            debug_views_specified ? "--debug-views" : result.options.effect_lines ? "--effect-lines" : realism ? "--realism" : nullptr;
        if (conflict)
        {
            result.error = "--benchmark=NAME fixes the script, preset and pose and runs to its end; it cannot be combined with " +
                std::string(conflict) + ".";
            return result;
        }
        result.options.scenario = std::string(workload->scenario);
        result.options.preset = workload->preset;
        result.options.view = workload->view;
        if (!window_specified)
        {
            result.options.window = workload->window;
            result.options.borderless = result.options.borderless || workload->borderless;
        }
        if (!wind_specified) result.options.wind = workload->wind;
        model = workload->mask;
        scenario_specified = true;
    }
    result.options.layer_mask = model & ~disabled;
    const std::string legacy_reason = result.options.benchmark.empty() ? std::string(", which --realism=off turns off.") :
        "; --benchmark=" + result.options.benchmark + " runs the legacy model.";
    if ((disabled >> realism_layout::realism_mask_bit) != 0 && !realism_enabled(model))
    {
        result.error = "--disable names a part of the realism model (linger, rise, shear, variation, haze, waves, skirt, condensation, push, "
                       "shake, rotor or brownout); it needs the realism model" + legacy_reason;
    }
    if (is_helicopter_scenario(result.options.scenario) && !realism_enabled(model))
    {
        result.error = "--scenario=" + result.options.scenario + " flies the helicopter; it needs the realism model" + legacy_reason;
    }
    if (result.options.scenario == "jet-vapor" && !humidity_specified) result.options.humidity_percent = jet_vapor_humidity_percent;
    if (is_jet_scenario(result.options.scenario) && !realism_enabled(model))
    {
        result.error = "--scenario=" + result.options.scenario + " flies the jet; it needs the realism model" + legacy_reason;
    }
    if (linger_specified && !realism_enabled(model))
    {
        result.error = "--linger needs the realism model" + legacy_reason;
    }
    if (humidity_specified && !realism_enabled(model))
    {
        result.error = "--humidity needs the realism model" + legacy_reason;
    }
    if (realism_enabled(model) && result.options.scalability.slot_policy == SlotPolicy::evict_oldest)
    {
        result.error = "--slot-policy=evict-oldest respawns the oldest effect's slab at once (a pop); use graduate-oldest or drop-new.";
    }
    if (!realism_enabled(model) && result.options.scalability.slot_policy == SlotPolicy::graduate_oldest)
    {
        result.error = "--slot-policy=graduate-oldest needs the realism model" + legacy_reason;
    }
    if (realism_enabled(model) && !policy_specified) result.options.scalability.slot_policy = SlotPolicy::graduate_oldest;
    if (capture_specified && !result.options.profile_tick && !(scenario_specified && result.options.capture_tick))
    {
        result.error = result.options.benchmark.empty() ?
            "--capture requires --profile-tick, or --scenario with --capture-tick, so the captured state is reproducible." :
            "--capture with --benchmark requires --capture-tick=N.";
    }
    if (result.options.capture_tick && profile_specified)
    {
        result.error = "--capture-tick cannot be combined with --profile-tick; a frozen scenario captures its frozen frame.";
    }
    if (result.options.capture_tick && !(scenario_specified && capture_specified))
    {
        result.error = result.options.benchmark.empty() ? "--capture-tick requires --scenario and --capture." : "--capture-tick requires --capture.";
    }
    if (debug_views_specified && result.options.effect_lines)
    {
        result.error = "Give --effect-lines or --debug-views, not both.";
    }
    if (debug_views_specified && scenario_specified)
    {
        result.error = "--debug-views cannot be combined with --scenario; use --effect-lines.";
    }
    if (scenario_specified && explosions_specified)
    {
        result.error = "--scenario cannot be combined with --explosions.";
    }
    if (profile_specified && !scenario_specified && *result.options.profile_tick > maximum_profile_tick)
    {
        result.error = "Expected --profile-tick=N with an integer from 0 to 480 (1 up to the scenario's duration with --scenario).";
    }
    if (profile_specified && scenario_specified && *result.options.profile_tick == 0)
    {
        result.error = "With --scenario, --profile-tick starts at 1: a scenario's first frame draws tick 1.";
    }
    if (result.options.camera && view_specified)
    {
        result.error = "Give --view or --camera, not both.";
    }
    if (result.options.scalability.half_light && result.options.scalability.smoke_resolution == SmokeResolution::full)
    {
        result.error = "--half-light requires --smoke-resolution=half or auto.";
    }
    if (budget_specified && result.options.scalability.smoke_resolution != SmokeResolution::automatic)
    {
        result.error = "--smoke-budget requires --smoke-resolution=auto.";
    }
    if (!result.options.counter_log.empty() && !profile_specified && !scenario_specified)
    {
        result.error = "--counter-log requires --scenario or --profile-tick.";
    }
    const bool sequence = !result.options.capture_sequence.empty();
    if ((result.options.capture_from || result.options.capture_to) && !sequence)
    {
        result.error = "--capture-from and --capture-to require --capture-sequence.";
    }
    if (sequence && result.options.capture_from && result.options.capture_to && *result.options.capture_from > *result.options.capture_to)
    {
        result.error = "--capture-from must not be later than --capture-to.";
    }
    if (sequence && result.options.exit_after)
    {
        result.error = "--capture-sequence ends at --capture-to; it cannot be combined with --exit-after.";
    }
    if (sequence && (!result.options.counter_log.empty() || result.options.ui.value_or(false)))
    {
        result.error = "--capture-sequence prints no record and draws no overlay; it cannot be combined with --counter-log or --ui=on.";
    }
    if (sequence && profile_specified)
    {
        result.error = "--capture-sequence cannot be combined with --profile-tick: a frozen run draws one state.";
    }
    if (sequence && (capture_specified || result.options.capture_tick))
    {
        result.error = "Give --capture-sequence or --capture, not both.";
    }
    if (sequence && !scenario_specified)
    {
        result.error = "--capture-sequence requires --scenario or --benchmark.";
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
    case RangeView::range: return "range";
    case RangeView::yard: return "yard";
    case RangeView::far: return "far";
    }
    return "overview";
}
}
