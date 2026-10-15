#include "command_line.hpp"

#include "../simulation/explosion_scenarios.hpp"

#include <array>
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

CommandLineResult parse_command_line(std::span<const char* const> arguments)
{
    CommandLineResult result;
    bool renderer_specified = false;
    bool explosions_specified = false;
    bool profile_specified = false;
    bool smoke_specified = false;
    bool scene_format_specified = false;
    bool scenario_specified = false;
    bool debug_views_specified = false;
    bool preset_specified = false;
    bool disable_specified = false;
    bool wind_specified = false;
    bool capture_specified = false;
    bool view_specified = false;
    bool budget_specified = false;
    bool order_specified = false;
    bool policy_specified = false;
    bool window_specified = false;
    std::optional<bool> realism;
    bool linger_specified = false;
    bool humidity_specified = false;
    std::uint32_t disabled = 0;
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
        else if (option == "--half-light")
        {
            if (!set_once(result.options.scalability.half_light, "--half-light")) return result;
        }
        else if (option == "--coverage")
        {
            if (!set_once(result.options.scalability.coverage, "--coverage")) return result;
        }
        else if (option == "--borderless")
        {
            if (!set_once(result.options.borderless, "--borderless")) return result;
        }
        else if (option.starts_with("--window="))
        {
            if (window_specified)
            {
                result.error = "Specify --window only once.";
                return result;
            }
            window_specified = true;
            const auto value = option.substr(std::string_view("--window=").size());
            const auto separator = value.find('x');
            const auto width = separator == std::string_view::npos ? std::nullopt :
                parse_count(value.substr(0, separator), minimum_window.width, maximum_window.width);
            const auto height = separator == std::string_view::npos ? std::nullopt :
                parse_count(value.substr(separator + 1), minimum_window.height, maximum_window.height);
            if (!width || !height)
            {
                result.error = "Expected --window=WxH in pixels, from 640x360 to 7680x4320.";
                return result;
            }
            result.options.window = {*width, *height};
        }
        else if (option == "--effect-lines")
        {
            if (!set_once(result.options.effect_lines, "--effect-lines")) return result;
        }
        else if (option.starts_with("--debug-views="))
        {
            if (debug_views_specified)
            {
                result.error = "Specify --debug-views only once.";
                return result;
            }
            debug_views_specified = true;
            const auto list = option.substr(std::string_view("--debug-views=").size());
            auto& views = result.options.debug_views;
            std::size_t start = 0;
            while (true)
            {
                const auto comma = list.find(',', start);
                const auto name = list.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
                if (name == "bounds") views.effect_bounds = true;
                else if (name == "boxes") views.collision_boxes = true;
                else if (name == "regions") views.spawn_regions = true;
                else if (name == "wind") views.wind_arrow = true;
                else if (name == "all") views = {true, true, true, true};
                else
                {
                    result.error = "Expected --debug-views=bounds, boxes, regions, wind or all, comma-separated.";
                    return result;
                }
                if (comma == std::string_view::npos) break;
                start = comma + 1;
            }
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
        else if (option.starts_with("--realism="))
        {
            if (realism)
            {
                result.error = "Specify --realism only once.";
                return result;
            }
            const auto value = option.substr(std::string_view("--realism=").size());
            if (value != "on" && value != "off")
            {
                result.error = "Expected --realism=on or off.";
                return result;
            }
            realism = value == "on";
        }
        else if (option.starts_with("--linger="))
        {
            if (linger_specified)
            {
                result.error = "Specify --linger only once.";
                return result;
            }
            linger_specified = true;
            const auto seconds = parse_count(option.substr(std::string_view("--linger=").size()), 10, 300);
            if (!seconds)
            {
                result.error = "Expected --linger=S, seconds from 10 to 300.";
                return result;
            }
            result.options.linger_seconds = *seconds;
        }
        else if (option.starts_with("--humidity="))
        {
            if (humidity_specified)
            {
                result.error = "Specify --humidity only once.";
                return result;
            }
            humidity_specified = true;
            const auto percent = parse_count(option.substr(std::string_view("--humidity=").size()), 0, 100);
            if (!percent)
            {
                result.error = "Expected --humidity=P, relative humidity in percent from 0 to 100.";
                return result;
            }
            result.options.humidity_percent = *percent;
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
            else if (value == "range") result.options.view = RangeView::range;
            else if (value == "yard") result.options.view = RangeView::yard;
            else if (value == "far") result.options.view = RangeView::far;
            else
            {
                result.error = "Expected --view=overview, ground, hill, aerial, range, yard or far.";
                return result;
            }
        }
        else if (option.starts_with("--camera="))
        {
            if (result.options.camera)
            {
                result.error = "Specify --camera only once.";
                return result;
            }
            std::array<float, 6> pose{};
            const bool parsed = parse_numbers(option.substr(std::string_view("--camera=").size()), pose);
            const float x = pose[3] - pose[0];
            const float y = pose[4] - pose[1];
            const float z = pose[5] - pose[2];
            if (!parsed || x * x + y * y + z * z < 1e-4f)
            {
                result.error = "Expected --camera=EX,EY,EZ,TX,TY,TZ: an eye and a different target in metres.";
                return result;
            }
            result.options.camera = pose;
        }
        else if (option.starts_with("--counter-log="))
        {
            if (!result.options.counter_log.empty())
            {
                result.error = "Specify --counter-log only once.";
                return result;
            }
            result.options.counter_log = option.substr(std::string_view("--counter-log=").size());
            if (result.options.counter_log.empty())
            {
                result.error = "Expected --counter-log=<file.csv>.";
                return result;
            }
        }
        else if (option.starts_with("--benchmark="))
        {
            if (!result.options.benchmark.empty())
            {
                result.error = "Specify --benchmark only once.";
                return result;
            }
            const auto value = option.substr(std::string_view("--benchmark=").size());
            if (!benchmark_workload(value))
            {
                result.error = "Expected --benchmark=" + or_list(benchmark_names) + ".";
                return result;
            }
            result.options.benchmark = std::string(value);
        }
        else if (option.starts_with("--scenario="))
        {
            if (scenario_specified)
            {
                result.error = "Specify --scenario only once.";
                return result;
            }
            scenario_specified = true;
            result.options.scenario = option.substr(std::string_view("--scenario=").size());
            if (!is_scenario_name(result.options.scenario))
            {
                result.error = "Expected --scenario=" + or_list(scenario_names) + ".";
                return result;
            }
        }
        else if (option.starts_with("--preset="))
        {
            if (preset_specified)
            {
                result.error = "Specify --preset only once.";
                return result;
            }
            preset_specified = true;
            const auto preset = preset_from_name(option.substr(std::string_view("--preset=").size()));
            if (!preset)
            {
                result.error = "Expected --preset=" + or_list(preset_names) + ".";
                return result;
            }
            result.options.preset = *preset;
        }
        else if (option.starts_with("--wind="))
        {
            if (wind_specified)
            {
                result.error = "Specify --wind only once.";
                return result;
            }
            wind_specified = true;
            float parsed[3]{};
            const bool valid = parse_numbers(option.substr(std::string_view("--wind=").size()), parsed);
            const auto wind = valid ? validated({parsed[0], parsed[1], parsed[2]}) : std::nullopt;
            if (!wind)
            {
                result.error = "Expected --wind=SPEED,DIRECTION,GUSTINESS (0-40 m/s, degrees, 0-1).";
                return result;
            }
            result.options.wind = *wind;
        }
        else if (option.starts_with("--disable="))
        {
            if (disable_specified)
            {
                result.error = "Specify --disable only once.";
                return result;
            }
            disable_specified = true;
            const auto list = option.substr(std::string_view("--disable=").size());
            for (std::size_t start = 0;;)
            {
                const auto comma = list.find(',', start);
                const auto name = list.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
                const auto bit = disable_bit(name);
                if (!bit)
                {
                    result.error = "Expected --disable=<comma-separated " + or_list(disable_names) + ">.";
                    return result;
                }
                disabled |= 1u << *bit;
                if (comma == std::string_view::npos) break;
                start = comma + 1;
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
            auto& resolution = result.options.scalability.smoke_resolution;
            if (value == "half") resolution = SmokeResolution::half;
            else if (value == "auto") resolution = SmokeResolution::automatic;
            else if (value != "full")
            {
                result.error = "Expected --smoke-resolution=full, half or auto.";
                return result;
            }
        }
        else if (option.starts_with("--smoke-lod=") || option.starts_with("--faint-floor="))
        {
            const bool lod = option.starts_with("--smoke-lod=");
            float& setting = lod ? result.options.scalability.smoke_lod : result.options.scalability.faint_floor;
            if (setting != 0)
            {
                result.error = lod ? "Specify --smoke-lod only once." : "Specify --faint-floor only once.";
                return result;
            }
            float value[1]{};
            const auto text = option.substr(option.find('=') + 1);
            if (!parse_numbers(text, value) || value[0] <= 0 || (!lod && value[0] >= 1) || (lod && value[0] > 4096))
            {
                result.error = lod ? "Expected --smoke-lod=P, a radius in pixels above 0 and at most 4096." : "Expected --faint-floor=F, an opacity between 0 and 1.";
                return result;
            }
            setting = value[0];
        }
        else if (option.starts_with("--slot-policy="))
        {
            if (policy_specified)
            {
                result.error = "Specify --slot-policy only once.";
                return result;
            }
            policy_specified = true;
            const auto value = option.substr(std::string_view("--slot-policy=").size());
            if (value == "evict-oldest") result.options.scalability.slot_policy = SlotPolicy::evict_oldest;
            else if (value == "graduate-oldest") result.options.scalability.slot_policy = SlotPolicy::graduate_oldest;
            else if (value != "drop-new")
            {
                result.error = "Expected --slot-policy=drop-new, evict-oldest or graduate-oldest.";
                return result;
            }
        }
        else if (option.starts_with("--effect-order="))
        {
            if (order_specified)
            {
                result.error = "Specify --effect-order only once.";
                return result;
            }
            order_specified = true;
            const auto value = option.substr(std::string_view("--effect-order=").size());
            auto& order = result.options.scalability.effect_order;
            if (value == "none") order = EffectOrder::none;
            else if (value == "cloud") order = EffectOrder::cloud;
            else if (value != "origin")
            {
                result.error = "Expected --effect-order=origin, none or cloud.";
                return result;
            }
        }
        else if (option.starts_with("--smoke-budget="))
        {
            if (budget_specified)
            {
                result.error = "Specify --smoke-budget only once.";
                return result;
            }
            budget_specified = true;
            const auto value = option.substr(std::string_view("--smoke-budget=").size());
            const bool count = value.starts_with("count:");
            float limit[1]{};
            const auto number = value.substr(value.find(':') == std::string_view::npos ? value.size() : value.find(':') + 1);
            if ((!count && !value.starts_with("coverage:")) || !parse_numbers(number, limit) || limit[0] <= 0)
            {
                result.error = "Expected --smoke-budget=coverage:SCREENS or count:PARTICLES, a positive number.";
                return result;
            }
            result.options.scalability.smoke_budget = {count, limit[0]};
        }
        else if (option.starts_with("--profile-tick="))
        {
            if (profile_specified)
            {
                result.error = "Specify --profile-tick only once.";
                return result;
            }
            profile_specified = true;
            result.options.profile_tick = parse_count(option.substr(std::string_view("--profile-tick=").size()), 0, 100'000);
            if (!result.options.profile_tick)
            {
                result.error = "Expected --profile-tick=N with an integer from 0 to 480 (1 up to the scenario's duration with --scenario).";
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
        else if (option.starts_with("--capture-sequence="))
        {
            if (!result.options.capture_sequence.empty())
            {
                result.error = "Specify --capture-sequence only once.";
                return result;
            }
            result.options.capture_sequence = option.substr(std::string_view("--capture-sequence=").size());
            if (result.options.capture_sequence.empty())
            {
                result.error = "Expected --capture-sequence=<folder>.";
                return result;
            }
        }
        else if (option.starts_with("--capture-from=") || option.starts_with("--capture-to="))
        {
            const bool from = option.starts_with("--capture-from=");
            auto& setting = from ? result.options.capture_from : result.options.capture_to;
            if (setting)
            {
                result.error = from ? "Specify --capture-from only once." : "Specify --capture-to only once.";
                return result;
            }
            setting = parse_count(option.substr(option.find('=') + 1), 1, 100'000);
            if (!setting)
            {
                result.error = from ? "Expected --capture-from=T with an integer from 1 to 100000." :
                    "Expected --capture-to=T with an integer from 1 to 100000.";
                return result;
            }
        }
        else if (option.starts_with("--capture-tick="))
        {
            if (result.options.capture_tick)
            {
                result.error = "Specify --capture-tick only once.";
                return result;
            }
            result.options.capture_tick = parse_count(option.substr(std::string_view("--capture-tick=").size()), 1, 100'000);
            if (!result.options.capture_tick)
            {
                result.error = "Expected --capture-tick=N with an integer from 1 to 100000.";
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
