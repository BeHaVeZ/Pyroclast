#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "command_line.hpp"
#include "counter_log.hpp"
#include "explosion_renderer.hpp"
#include "frame_baseline.hpp"
#include "frame_statistics.hpp"
#include "key_bindings.hpp"
#include "sandbox.hpp"
#include "../simulation/explosion_scenarios.hpp"
#include "../tools/debug_ui.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace
{
int save_capture(pyroclast::GraphicsDevice& device, const std::string& path)
{
    auto image = device.read_capture();
    if (!image)
    {
        return 1;
    }
    SDL_Surface* surface = SDL_CreateSurfaceFrom(static_cast<int>(image->width), static_cast<int>(image->height),
        SDL_PIXELFORMAT_RGBA32, image->rgba.data(), static_cast<int>(image->width * 4));
    const bool saved = surface && SDL_SavePNG(surface, path.c_str());
    SDL_DestroySurface(surface);
    if (!saved)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Save capture '%s' failed: %s", path.c_str(), SDL_GetError());
        return 1;
    }
    SDL_Log("Captured frame: %s (%ux%u).", path.c_str(), image->width, image->height);
    return 0;
}

struct SdlCleanup final
{
    SdlCleanup() = default;
    SdlCleanup(const SdlCleanup&) = delete;
    SdlCleanup& operator=(const SdlCleanup&) = delete;

    ~SdlCleanup()
    {
        SDL_Quit();
    }
};

struct FrameHistory
{
    std::array<float, 240> milliseconds{};
    std::size_t count = 0;
    std::size_t next = 0;
    void add(double value)
    {
        milliseconds[next] = static_cast<float>(value);
        next = (next + 1) % milliseconds.size();
        count = std::min(count + 1, milliseconds.size());
    }
    std::span<const float> samples() const { return {milliseconds.data(), count}; }
    int offset() const { return count < milliseconds.size() ? 0 : static_cast<int>(next); }
};

using pyroclast::Sandbox;

const char* pose_name(const pyroclast::ApplicationOptions& options)
{
    return options.camera ? "custom" : pyroclast::view_name(options.view);
}

class ScenarioRun final
{
public:
    explicit ScenarioRun(pyroclast::Scenario scenario) : scenario_(std::move(scenario)) {}
    bool finished(const Sandbox& sandbox) const { return sandbox.particles.tick() >= scenario_.duration_ticks; }
    void invalidate(const char* reason)
    {
        if (valid_) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Scenario measurement invalidated: %s.", reason);
        valid_ = false;
    }
    void submit_due(Sandbox& sandbox)
    {
        while (next_ < scenario_.events.size() && scenario_.events[next_].tick <= sandbox.particles.tick())
        {
            sandbox.effects.submit(scenario_.events[next_++].event);
        }
    }
    void record(double cpu_ms, std::optional<pyroclast::GpuTimings> gpu, const Sandbox& sandbox)
    {
        cpu_ms_.push_back(cpu_ms);
        if (gpu && gpu->sample_id != last_gpu_sample_)
        {
            last_gpu_sample_ = gpu->sample_id;
            draw_ms_.push_back(gpu->draw_ms);
            compute_ms_.push_back(gpu->compute_ms);
            for (std::uint32_t index = 0; index < gpu->region_count; ++index)
            {
                const auto& region = gpu->regions[index];
                auto found = std::find_if(regions_.begin(), regions_.end(), [&region](const RegionSum& sum) { return sum.label == region.label; });
                if (found == regions_.end()) found = regions_.insert(regions_.end(), {region.label, 0, 0});
                found->milliseconds += region.milliseconds;
                ++found->samples;
            }
        }
        peak_effects_ = std::max(peak_effects_, static_cast<std::uint32_t>(sandbox.effects.active().size()));
    }
    void count(const pyroclast::ParticleCounters& counters)
    {
        ++counter_samples_;
        peak_alive_ = std::max(peak_alive_, counters.alive);
        alive_sum_ += counters.alive;
        checksum_ = pyroclast::counter_checksum(checksum_, counters);
    }
    int report(const Sandbox& sandbox, const pyroclast::ApplicationOptions& options, std::string_view gpu_name, int width, int height,
        bool overlay, const std::string& extra)
    {
        const auto statistics = [](std::vector<double>& samples) {
            if (samples.empty()) return std::string("null");
            const auto summary = pyroclast::summarize(samples);
            char text[160];
            std::snprintf(text, sizeof(text), "{\"average\":%.6f,\"median\":%.6f,\"p95\":%.6f,\"p99\":%.6f,\"max\":%.6f}",
                summary.average, summary.median, summary.p95, summary.p99, summary.max);
            return std::string(text);
        };
        const auto frames = cpu_ms_.size();
        SDL_Log("Scenario %.*s finished: %zu frames, %llu effects started, %llu dropped, peak %u active.",
            static_cast<int>(scenario_.name.size()), scenario_.name.data(), frames,
            static_cast<unsigned long long>(sandbox.effects.started()), static_cast<unsigned long long>(sandbox.effects.dropped()),
            peak_effects_);
        const double mean_alive = counter_samples_ != 0 ? static_cast<double>(alive_sum_) / static_cast<double>(counter_samples_) : 0.0;
        SDL_Log("Live particles: %zu counter samples, peak %u alive, mean %.1f, checksum %016llx.", counter_samples_, peak_alive_,
            mean_alive, static_cast<unsigned long long>(checksum_));
        std::string gpu = "\"";
        for (const char character : gpu_name) if (character != '"' && character != '\\' && static_cast<unsigned char>(character) >= 0x20) gpu += character;
        gpu += '"';
        const auto cpu = statistics(cpu_ms_);
        const auto draw = statistics(draw_ms_);
        const auto compute = statistics(compute_ms_);
        std::string regions;
        for (const auto& sum : regions_)
        {
            char entry[160];
            std::snprintf(entry, sizeof(entry), "%s{\"label\":\"%s\",\"ms\":%.6f,\"samples\":%zu}", regions.empty() ? "" : ",",
                sum.label, sum.milliseconds / static_cast<double>(sum.samples), sum.samples);
            regions += entry;
        }
        std::string camera = options.camera ? ",\"camera\":" + pyroclast::camera_json(*options.camera) : std::string();
        if (!extra.empty()) camera += "," + extra;
        std::printf("{\"scenario\":\"%.*s\",\"valid\":%s,\"build\":\"%s\",\"renderer\":\"%s\",\"gpu\":%s,\"width\":%d,\"height\":%d,"
            "\"smoke_resolution\":\"%s\",\"scene_format\":\"%s\",\"view\":\"%s\"%s,\"overlay\":%s,\"effect_lines\":%s,\"gpu_regions\":%s,"
            "\"preset\":\"%.*s\",\"layer_mask\":%u,\"wind\":[%.2f,%.1f,%.2f],\"ticks\":%u,\"frames\":%zu,\"effects_started\":%llu,\"effects_dropped\":%llu,\"effects_evicted\":%llu,\"peak_effects\":%u,"
            "\"particles\":{\"samples\":%zu,\"peak_alive\":%u,\"mean_alive\":%.3f,\"checksum\":\"%016llx\"},\"cpu_ms\":%s,"
            "\"gpu_ms\":{\"samples\":%zu,\"draw\":%s,\"compute\":%s,\"regions\":[%s]}}\n",
            static_cast<int>(scenario_.name.size()), scenario_.name.data(), valid_ ? "true" : "false", pyroclast::build_configuration,
            pyroclast::renderer_name(options.renderer), gpu.c_str(), width, height, pyroclast::smoke_resolution_name(options.scalability.smoke_resolution),
            pyroclast::scene_format_name(options.scene_format), pose_name(options), camera.c_str(), overlay ? "true" : "false",
            sandbox.views.effect_bounds ? "true" : "false", options.gpu_regions ? "true" : "false",
            static_cast<int>(pyroclast::preset_name(options.preset).size()), pyroclast::preset_name(options.preset).data(),
            sandbox.layer_mask, static_cast<double>(sandbox.wind.speed), static_cast<double>(sandbox.wind.direction_degrees),
            static_cast<double>(sandbox.wind.gustiness), scenario_.duration_ticks, frames,
            static_cast<unsigned long long>(sandbox.effects.started()), static_cast<unsigned long long>(sandbox.effects.dropped()),
            static_cast<unsigned long long>(sandbox.effects.evicted()), peak_effects_, counter_samples_, peak_alive_, mean_alive, static_cast<unsigned long long>(checksum_), cpu.c_str(),
            draw_ms_.size(), draw.c_str(), compute.c_str(), regions.c_str());
        std::fflush(stdout);
        return valid_ ? 0 : 1;
    }

private:
    pyroclast::Scenario scenario_;
    std::size_t next_ = 0;
    std::vector<double> cpu_ms_;
    std::vector<double> draw_ms_;
    std::vector<double> compute_ms_;
    std::uint64_t last_gpu_sample_ = 0;
    struct RegionSum
    {
        const char* label;
        double milliseconds;
        std::size_t samples;
    };
    std::vector<RegionSum> regions_;
    std::uint32_t peak_effects_ = 0;
    std::size_t counter_samples_ = 0;
    std::uint32_t peak_alive_ = 0;
    std::uint64_t alive_sum_ = 0;
    std::uint64_t checksum_ = pyroclast::checksum_basis;
    bool valid_ = true;
};

struct FlightControls
{
    bool looking = false;
    pyroclast::float2 look;

    void stop_looking(SDL_Window* window)
    {
        if (looking) SDL_SetWindowRelativeMouseMode(window, false);
        looking = false;
    }
    pyroclast::CameraInput input(bool keyboard)
    {
        pyroclast::CameraInput result;
        if (keyboard)
        {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            const auto axis = [keys](SDL_Scancode positive, SDL_Scancode negative) {
                return static_cast<float>(keys[positive]) - static_cast<float>(keys[negative]);
            };
            result.move = {axis(SDL_SCANCODE_D, SDL_SCANCODE_A), axis(SDL_SCANCODE_E, SDL_SCANCODE_Q), axis(SDL_SCANCODE_W, SDL_SCANCODE_S)};
            result.fast = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
        }
        result.look = look;
        look = {};
        return result;
    }
};

pyroclast::Camera camera_pose(const std::array<float, 6>& pose)
{
    pyroclast::Camera camera;
    camera.position = {pose[0], pose[1], pose[2]};
    camera.look_at({pose[3], pose[4], pose[5]});
    return camera;
}

int run_event_loop(SDL_Window* window, pyroclast::GraphicsDevice& renderer, Sandbox& sandbox, pyroclast::DebugUi* ui,
    const pyroclast::ApplicationOptions& options, std::optional<ScenarioRun> scenario, pyroclast::CounterLog* counter_log)
{
    const auto window_id = SDL_GetWindowID(window);
    auto& demo = sandbox.particles;
    {
        std::array<pyroclast::Camera, pyroclast::CameraRig::bookmark_count> views;
        std::array<std::string_view, pyroclast::CameraRig::bookmark_count> names;
        for (std::size_t index = 0; index < views.size(); ++index)
        {
            const auto view = static_cast<pyroclast::RangeView>(index);
            views[index] = sandbox.range.view(view);
            names[index] = pyroclast::view_name(view);
        }
        sandbox.camera = pyroclast::CameraRig(options.camera ? camera_pose(*options.camera) : sandbox.range.view(options.view), views, names);
    }
    FlightControls flight;
    const auto profile_tick = options.profile_tick;
    const bool frozen = scenario && profile_tick;
    const bool unattended = scenario.has_value() || !options.capture_path.empty();
    pyroclast::FrameBaseline baseline({pyroclast::renderer_name(options.renderer), std::string(renderer.adapter_name()),
        static_cast<std::uint32_t>(sandbox.group.size()), pyroclast::ParticleSystem::capacity, profile_tick,
        pyroclast::smoke_resolution_name(options.scalability.smoke_resolution),
        pose_name(options), options.camera, frozen ? options.scenario : std::string(), pyroclast::scene_format_name(options.scene_format),
        std::string(pyroclast::preset_name(options.preset)), options.layer_mask, options.wind});
    if (scenario && !frozen) baseline.disable();
    if (ui)
    {
        baseline.disable();
        SDL_Log("Startup sample skipped while the overlay is on; launch with --ui=off to take it.");
    }
    FrameHistory history;
    FrameHistory gpu_history;
    std::uint64_t last_gpu_sample = 0;
    pyroclast::TickRate tick_rate;
    std::uint32_t last_steps = 0;
    double showcase_ms = 0;
    pyroclast::CoverageStatistics coverage;
    const bool automatic = options.scalability.smoke_resolution == pyroclast::SmokeResolution::automatic;
    const auto scalability_json = [&coverage, &sandbox, automatic]() {
        std::string text = coverage.json();
        if (automatic)
        {
            const auto& policy = sandbox.renderer.smoke_policy();
            text += (text.empty() ? "" : ",") + std::string("\"half_frames\":") + std::to_string(policy.half_frames()) +
                ",\"switches\":" + std::to_string(policy.switches());
        }
        const auto& settings = sandbox.scalability;
        std::string options;
        char number[64];
        const auto add = [&options](const std::string& entry) { options += (options.empty() ? "" : ",") + entry; };
        if (settings.smoke_budget.count || settings.smoke_budget.limit > 0)
        {
            std::snprintf(number, sizeof(number), "%s:%g", settings.smoke_budget.count ? "count" : "coverage",
                pyroclast::SmokeResolutionPolicy::limit(settings.smoke_budget));
            add(std::string("\"smoke_budget\":\"") + number + "\"");
        }
        if (settings.half_light) add("\"half_light\":true");
        if (settings.smoke_lod > 0)
        {
            std::snprintf(number, sizeof(number), "%g", static_cast<double>(settings.smoke_lod));
            add(std::string("\"smoke_lod\":") + number);
        }
        if (settings.faint_floor > 0)
        {
            std::snprintf(number, sizeof(number), "%g", static_cast<double>(settings.faint_floor));
            add(std::string("\"faint_floor\":") + number);
        }
        if (settings.effect_order != pyroclast::EffectOrder::origin)
        {
            add(std::string("\"effect_order\":\"") + pyroclast::effect_order_name(settings.effect_order) + "\"");
        }
        if (settings.slot_policy != pyroclast::SlotPolicy::drop_new)
        {
            add(std::string("\"slot_policy\":\"") + pyroclast::slot_policy_name(settings.slot_policy) + "\"");
        }
        if (!options.empty()) text += (text.empty() ? "" : ",") + std::string("\"scalability\":{") + options + "}";
        return text;
    };
    baseline.set_extra(scalability_json);
    std::vector<pyroclast::UiCommand> commands;
    auto previous_time = SDL_GetPerformanceCounter();
    std::uint32_t submitted_frames = 0;
    const auto finish = [&options, &scenario, frozen]() {
        if (scenario && !frozen)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Exited before scenario %s finished; no summary.", options.scenario.c_str());
            return 1;
        }
        if (options.capture_path.empty()) return 0;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Exited before the capture at tick %u was drawn.",
            options.capture_tick ? *options.capture_tick : *options.profile_tick);
        return 1;
    };
    while (true)
    {
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                    event.window.windowID == window_id))
            {
                return finish();
            }
            if ((event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT) ||
                event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
            {
                flight.stop_looking(window);
            }
            if (ui && ui->process_event(event)) continue;
            if (ui && event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F1 && !event.key.repeat) ui->toggle_visible();
            if (event.type == SDL_EVENT_WINDOW_MINIMIZED || event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
                (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && (event.window.data1 != 1280 || event.window.data2 != 720)))
            {
                baseline.disable();
                if (scenario) scenario->invalidate("the window was minimized, unfocused or resized");
            }
            if (unattended) continue;
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.windowID == window_id && event.button.button == SDL_BUTTON_RIGHT)
            {
                flight.looking = SDL_SetWindowRelativeMouseMode(window, true);
                baseline.disable();
            }
            if (event.type == SDL_EVENT_MOUSE_MOTION && flight.looking)
            {
                flight.look.x += event.motion.xrel;
                flight.look.y += event.motion.yrel;
            }
            if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.windowID == window_id)
            {
                sandbox.camera.scale_speed(event.wheel.y);
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.windowID == window_id &&
                event.button.button == SDL_BUTTON_LEFT && flight.looking)
            {
                sandbox.spawn(pyroclast::crosshair_ray(sandbox.camera.camera()), "crosshair");
                baseline.disable();
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.windowID == window_id &&
                event.button.button == SDL_BUTTON_LEFT)
            {
                int window_width = 0;
                int window_height = 0;
                if (SDL_GetWindowSize(window, &window_width, &window_height) && window_width > 0 && window_height > 0)
                {
                    const float w = static_cast<float>(window_width);
                    const float h = static_cast<float>(window_height);
                    const auto ndc = pyroclast::window_to_ndc(event.button.x, event.button.y, w, h);
                    sandbox.spawn(sandbox.camera.camera().ray_through(ndc.x, ndc.y, w / h), "cursor");
                    baseline.disable();
                }
            }
            if (event.type == SDL_EVENT_KEY_DOWN) baseline.disable();
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.windowID == window_id && !event.key.repeat)
            {
                if (const auto command = pyroclast::key_command(event.key.key, event.key.mod))
                {
                    pyroclast::apply(*command, sandbox);
                    if (command->kind == pyroclast::UiCommand::Kind::recall_bookmark) flight.look = {};
                }
            }
        }

        const bool minimized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0;
        if (!minimized)
        {
            int width = 0;
            int height = 0;
            if (!SDL_GetWindowSizeInPixels(window, &width, &height))
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "Read window pixel size failed: %s", SDL_GetError());
                return 1;
            }
            if (width > 0 && height > 0)
            {
                if (width != 1280 || height != 720)
                {
                    baseline.disable();
                    if (scenario) scenario->invalidate("the window is not 1280x720");
                }
                const auto start = SDL_GetPerformanceCounter();
                if (!renderer.resize(width, height))
                {
                    return 1;
                }
                const double elapsed = static_cast<double>(start - previous_time) /
                    static_cast<double>(SDL_GetPerformanceFrequency());
                previous_time = start;
                const auto input = flight.input(!unattended && (!ui || !ui->wants_keyboard()));
                if (sandbox.camera.update(input, static_cast<float>(elapsed), sandbox.range.environment(), sandbox.props_shown()))
                {
                    baseline.disable();
                }
                const auto& camera = sandbox.camera.camera();
                const auto showcase_start = SDL_GetPerformanceCounter();
                sandbox.update_aim((ui && ui->visible()) || sandbox.views.spawn_regions || sandbox.views.wind_arrow);
                auto showcase_ticks = SDL_GetPerformanceCounter() - showcase_start;
                ImDrawData* overlay = nullptr;
                if (ui)
                {
                    pyroclast::DiagnosticsSnapshot snapshot;
                    snapshot.system = {.renderer = pyroclast::renderer_name(options.renderer), .adapter = renderer.adapter_name(),
                        .build = pyroclast::build_configuration, .width = width, .height = height,
                        .vsync = !profile_tick.has_value() && !scenario, .scene_format = pyroclast::scene_format_name(options.scene_format),
                        .exposure = pyroclast::TestRange::exposure};
                    snapshot.frame = {.cpu_ms = history.samples(), .cpu_offset = history.offset(), .gpu_ms = gpu_history.samples(),
                        .gpu_offset = gpu_history.offset(), .gpu = renderer.completed_gpu_timings(), .showcase_cpu_ms = showcase_ms};
                    sandbox.fill_snapshot(snapshot);
                    snapshot.clock.steps = last_steps;
                    snapshot.clock.ticks_per_second = static_cast<float>(tick_rate.rate());
                    commands.clear();
                    overlay = ui->build(snapshot, commands);
                    if (!unattended) for (const auto command : commands) pyroclast::apply(command, sandbox);
                }
                const auto release_start = SDL_GetPerformanceCounter();
                if (scenario && (!frozen || demo.has_pending_steps())) scenario->submit_due(sandbox);
                else if (!unattended) sandbox.release_due();
                sandbox.build_marks();
                showcase_ticks += SDL_GetPerformanceCounter() - release_start;
                showcase_ms = 1000.0 * static_cast<double>(showcase_ticks) / static_cast<double>(SDL_GetPerformanceFrequency());
                const auto tick_before = demo.tick();
                const auto started_before = sandbox.effects.started();
                const auto dropped_before = sandbox.effects.dropped();
                const auto evicted_before = sandbox.effects.evicted();
                const bool capture = !options.capture_path.empty() &&
                    (options.capture_tick ? demo.tick() + 1 == *options.capture_tick : !demo.has_pending_steps());
                const auto result = sandbox.renderer.render(renderer, demo, sandbox.effects, sandbox.range, camera, width, height, elapsed,
                    {capture, overlay, sandbox.views, sandbox.layer_mask, sandbox.wind, sandbox.marks, sandbox.scalability, sandbox.counters});
                if (result == pyroclast::RenderResult::failed) return 1;
                if (!unattended)
                {
                    const double now = static_cast<double>(SDL_GetPerformanceCounter()) / static_cast<double>(SDL_GetPerformanceFrequency());
                    if (const auto report = sandbox.pressure.record(tick_before, now,
                            static_cast<std::uint32_t>(sandbox.effects.active().size()), sandbox.effects.dropped()))
                    {
                        sandbox.report_drops(*report);
                    }
                }
                last_steps = demo.tick() - tick_before;
                if (counter_log && result == pyroclast::RenderResult::submitted)
                {
                    counter_log->add_frame({demo.tick(), last_steps, static_cast<std::uint32_t>(sandbox.effects.active().size()),
                        sandbox.effects.started() - started_before, sandbox.effects.dropped() - dropped_before, !sandbox.effects.active().empty(),
                        sandbox.renderer.half_smoke(), sandbox.effects.evicted() - evicted_before});
                }
                const double wall = static_cast<double>(SDL_GetPerformanceCounter()) / static_cast<double>(SDL_GetPerformanceFrequency());
                tick_rate.add(wall, demo.tick());
                if (const auto gpu = renderer.completed_gpu_timings(); gpu && gpu->sample_id != last_gpu_sample)
                {
                    last_gpu_sample = gpu->sample_id;
                    gpu_history.add(gpu->compute_ms + gpu->draw_ms);
                    if (counter_log) counter_log->add_timing(gpu->draw_ms, gpu->compute_ms);
                }
                const auto counters = sandbox.poll_counters(renderer);
                if (scenario && !frozen && counters) scenario->count(*counters);
                if (counter_log && counters) counter_log->add_counters(*counters);
                if (counters && counters->measured && (!profile_tick || counters->tick == *profile_tick))
                {
                    coverage.add(counters->smoke_coverage, counters->light_coverage, counters->offscreen,
                        static_cast<double>(width) * static_cast<double>(height));
                }
                if (result == pyroclast::RenderResult::submitted)
                {
                    history.add(1000.0 * elapsed);
                    const double milliseconds = 1000.0 * static_cast<double>(SDL_GetPerformanceCounter() - start) /
                        static_cast<double>(SDL_GetPerformanceFrequency());
                    if (counter_log) counter_log->set_cpu_ms(milliseconds);
                    if (!profile_tick && baseline.enabled() && sandbox.effects.active().size() != sandbox.group.size())
                    {
                        baseline.disable();
                        SDL_Log("Startup sample stopped: the group retired before the sample completed; use --profile-tick or --scenario.");
                    }
                    if (!profile_tick || !demo.has_pending_steps())
                        baseline.record(milliseconds, renderer.completed_gpu_timings(), sandbox.effects.active().empty() ?
                            std::optional<std::uint32_t>(0) : sandbox.counters ? std::optional(sandbox.counters->alive) : std::nullopt);
                    if (capture) return save_capture(renderer, options.capture_path);
                    if (scenario && options.capture_tick && scenario->finished(sandbox))
                    {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Scenario %s ended before tick %u.", options.scenario.c_str(),
                            *options.capture_tick);
                        return 1;
                    }
                    if (scenario && !frozen)
                    {
                        scenario->record(milliseconds, renderer.completed_gpu_timings(), sandbox);
                        if (scenario->finished(sandbox))
                        {
                            return scenario->report(sandbox, options, renderer.adapter_name(), width, height, ui != nullptr, scalability_json());
                        }
                    }
                    if (options.exit_after && ++submitted_frames >= *options.exit_after)
                    {
                        SDL_Log("Exiting after %u submitted frames.", submitted_frames);
                        return finish();
                    }
                }
                else baseline.disable();
                continue;
            }
        }

        SDL_ClearError();
        if (!SDL_WaitEventTimeout(nullptr, 100) && SDL_GetError()[0] != '\0')
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "Event wait failed: %s", SDL_GetError());
            return 1;
        }
        previous_time = SDL_GetPerformanceCounter();
    }
}

int run_application(const pyroclast::ApplicationOptions& options)
{
    const SdlCleanup sdl_cleanup;
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SDL initialization failed: %s", SDL_GetError());
        return 1;
    }

    const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("Pyroclast", 1280, 720, SDL_WINDOW_RESIZABLE |
            (options.renderer == pyroclast::Renderer::vulkan ? SDL_WINDOW_VULKAN : 0)),
        &SDL_DestroyWindow);
    if (!window)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Window creation failed: %s", SDL_GetError());
        return 1;
    }

#ifdef NDEBUG
    if (options.gpu_validation)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "--gpu-validation requires a Debug build.");
        return 1;
    }
#endif
    const bool measured = options.profile_tick.has_value() || !options.scenario.empty();
    std::unique_ptr<pyroclast::DebugUi> ui;
    if (options.ui.value_or(!measured))
    {
        ui = pyroclast::DebugUi::create(window.get(), options.renderer);
        if (!ui) return 1;
    }
    const pyroclast::GraphicsDeviceOptions device_options{
        .gpu_timing = options.gpu_timing || measured || ui != nullptr,
        .vsync = !measured,
        .capture = !options.capture_path.empty(),
        .gpu_validation = options.gpu_validation,
        .gpu_regions = options.gpu_regions,
        .adapter = options.adapter,
        .debug_overlay = ui != nullptr};
    auto creation = pyroclast::create_graphics_device(options.renderer, window.get(), device_options);
    if (!creation.device)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", creation.error.c_str());
        return 1;
    }
    auto& device = *creation.device;
    SDL_Log("Smoke resolution: %s; scene target: %s.", pyroclast::smoke_resolution_name(options.scalability.smoke_resolution),
        pyroclast::scene_format_name(options.scene_format));
    const auto scene_format = options.scene_format == pyroclast::SceneFormat::rgba16f ? pyroclast::TargetFormat::rgba16_float :
        options.scene_format == pyroclast::SceneFormat::r11g11b10f ? pyroclast::TargetFormat::r11g11b10_float : pyroclast::TargetFormat::color;
    auto sandbox = std::make_unique<Sandbox>();
    if (!sandbox->range.initialize(device, scene_format) ||
        !sandbox->particles.initialize(device, sandbox->range.environment_buffer(), {scene_format, pyroclast::ExplosionRenderer::smoke_format},
            pyroclast::effect_classes()) ||
        !sandbox->renderer.initialize(device, scene_format))
    {
        return 1;
    }
    sandbox->views.effect_bounds = options.effect_lines || options.debug_views.effect_bounds;
    sandbox->views.collision_boxes = options.effect_lines || options.debug_views.collision_boxes;
    sandbox->views.spawn_regions = options.debug_views.spawn_regions;
    sandbox->views.wind_arrow = options.debug_views.wind_arrow;
    sandbox->showcase.preset = options.preset;
    sandbox->layer_mask = options.layer_mask;
    sandbox->wind = options.wind;
    sandbox->scalability = options.scalability;
    const auto preset = pyroclast::preset_name(options.preset);
    SDL_Log("Preset: %.*s; mask 0x%04x; wind %.1f m/s towards %.0f deg, gustiness %.2f.", static_cast<int>(preset.size()), preset.data(),
        options.layer_mask, options.wind.speed, options.wind.direction_degrees, options.wind.gustiness);
    std::optional<ScenarioRun> scenario;
    if (!options.scenario.empty())
    {
        auto script = pyroclast::make_scenario(options.scenario, sandbox->range.environment(), options.preset);
        if (!script) return 1;
        if (options.profile_tick && *options.profile_tick > script->duration_ticks)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Scenario %s runs %u ticks; --profile-tick=%u is past its end.", options.scenario.c_str(),
                script->duration_ticks, *options.profile_tick);
            return 1;
        }
        SDL_Log("Scenario %s: %zu events over %u ticks, one tick per frame; VSync off; timestamps on.", options.scenario.c_str(),
            script->events.size(), script->duration_ticks);
        scenario.emplace(std::move(*script));
        if (!options.profile_tick) sandbox->particles.set_fixed_ticks(true);
    }
    else
    {
        sandbox->group = pyroclast::group_events(sandbox->range.terrain(), options.explosions, options.preset);
        sandbox->start_group();
    }
    if (options.profile_tick)
    {
        sandbox->particles.prepare_profile(*options.profile_tick);
        SDL_Log("Fixed-state profile: tick=%u; VSync off; timestamps on; zero integration steps after preparation.", *options.profile_tick);
    }
    const std::string title = "Pyroclast | " + std::string(pyroclast::controls_summary()) + (ui ? " | F1: overlay" : "");
    SDL_SetWindowTitle(window.get(), title.c_str());
    SDL_Log("Explosion demo ready. %.*s.", static_cast<int>(pyroclast::controls_summary().size()), pyroclast::controls_summary().data());
    std::optional<pyroclast::CounterLog> counter_log;
    const bool automatic_smoke = options.scalability.smoke_resolution == pyroclast::SmokeResolution::automatic;
    if (!options.counter_log.empty())
    {
        const auto& settings = options.scalability;
        counter_log.emplace(settings.coverage || automatic_smoke, automatic_smoke, settings.smoke_lod > 0 || settings.faint_floor > 0,
            settings.slot_policy == pyroclast::SlotPolicy::evict_oldest);
    }
    int exit_code = run_event_loop(window.get(), device, *sandbox, ui.get(), options, std::move(scenario), counter_log ? &*counter_log : nullptr);
    if (counter_log)
    {
        std::ofstream file(options.counter_log, std::ios::binary);
        file << counter_log->csv();
        if (!file)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Write counter log '%s' failed.", options.counter_log.c_str());
            exit_code = 1;
        }
        else SDL_Log("Counter log: %zu frames written to %s.", counter_log->size(), options.counter_log.c_str());
        if (counter_log->mismatches() != 0)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Counter log: %zu samples or records did not match their frames.", counter_log->mismatches());
            exit_code = 1;
        }
    }
    const bool idle = device.wait_idle();
    sandbox.reset();
    return idle ? exit_code : 1;
}
}

int main(int argc, char** argv)
{
    const auto arguments = std::span<const char* const>(
        argv + 1, static_cast<std::size_t>(argc - 1));
    const auto result = pyroclast::parse_command_line(arguments);
    if (!result.error.empty())
    {
        std::fprintf(stderr, "Error: %s\n", result.error.c_str());
        return 1;
    }
    if (result.options.show_help)
    {
        std::puts(
            "Usage: Pyroclast.exe [--renderer=d3d12|vulkan] [--explosions=1|10|50|100] [--gpu-timing] [--gpu-regions] [--profile-tick=N]\n"
            "                     [--smoke-resolution=full|half|auto] [--ui=on|off] [--gpu-validation]\n"
            "                     [--capture=FILE.png] [--exit-after=N] [--view=overview|ground|hill|aerial|range|yard]\n"
            "                     [--scene-format=rgba16f|r11g11b10f|rgba8] [--scenario=NAME] [--capture-tick=N] [--effect-lines] [--help]\n"
            "  --renderer=d3d12   Request Direct3D 12 (default).\n"
            "  --renderer=vulkan  Request Vulkan.\n"
            "  --explosions=N     Simultaneous explosion group (1, 10, 50, 100; default 1).\n"
            "  --profile-tick=N   Freeze at tick 0-480; enable timestamps and disable VSync. With --scenario: prepare the\n"
            "                     scenario to tick N (1 up to its duration), then freeze and sample it the same way.\n"
            "  --smoke-resolution=full|half|auto  Smoke at full or half resolution (default full); auto draws it at half\n"
            "                     resolution while the measured smoke coverage is above a budget.\n"
            "  --smoke-budget=coverage:S|count:N  With auto: half resolution from S screens of smoke coverage (default 1000),\n"
            "                     or from N alive smoke particles.\n"
            "  --half-light       With half or auto: half-resolution frames draw light at half resolution too.\n"
            "  --effect-order=K   Sort effects back to front by origin (default), cloud (the rising cloud's centre) or none.\n"
            "  --slot-policy=P    With every effect slot busy: drop-new (default) drops the event, evict-oldest replaces the\n"
            "                     effect that started earliest.\n"
            "  --gpu-timing       Enable GPU timestamp measurements.\n"
            "  --gpu-regions      Also time every dispatch, pass and the overlay (serializes Vulkan GPU work).\n"
            "  --capture=FILE.png Write the first frame drawn at the profile tick, then exit (requires --profile-tick,\n"
            "                     or --scenario with --capture-tick).\n"
            "  --capture-tick=N   With --scenario and --capture: capture the frame that draws tick N, then exit.\n"
            "  --exit-after=N     Exit after N submitted frames.\n"
            "  --view=NAME        Start the camera at a named pose: overview (default), ground, hill, aerial, range\n"
            "                     (the north yard's props) or yard (close to its walls).\n"
            "  --camera=EX,EY,EZ,TX,TY,TZ  Start the camera at this eye, looking at this target (metres), instead of --view.\n"
            "  --scene-format=F   Scene target: rgba16f (default), r11g11b10f, or rgba8 (a cost reference; bands).\n"
            "  --scenario=NAME    Scripted run, one tick per frame, then a JSON summary: single, grid-10, grid-50,\n"
            "                     grid-100, barrage (80 impacts walking across the range), presets (all seven in a row)\n"
            "                     range (seven presets among the north yard's props), bombardment (400 impacts\n"
            "                     within 30 m, 20 a second: more than the 128 effect slots hold) or accumulation\n"
            "                     (impacts within 30 m as often as the 128 slots allow, for 30 s of steady state).\n"
            "  --preset=NAME      small-impact, small-he (default), artillery-he, heavy, fuel-fire, dust-blast or stress-test,\n"
            "                     for the startup group, profiles, scenarios and the first click.\n"
            "  --disable=LIST     Hide layers, the lights, smoke features or the props: comma-separated dust, smoke, plume,\n"
            "                     flash, fireball, sparks, debris, lights, curl, vortex, gusts, shading, shadow, emission,\n"
            "                     props (no props drawn, hit or collided with), collision (props drawn, particles pass),\n"
            "                     deflection (props do not turn the air aside).\n"
            "  --wind=S,D,G       Wind speed 0-40 m/s, heading in degrees from +x towards +z, gustiness 0-1 (default 3,0,0.3).\n"
            "  --effect-lines     Start with each effect's origin, normal and reach, and the props' collision boxes,\n"
            "                     drawn as lines (L toggles).\n"
            "  --debug-views=LIST Start with debug views on: comma-separated bounds (effect bounds), boxes (collision\n"
            "                     boxes), regions (where the showcase places events, F8), wind (wind arrow, F9), or all.\n"
            "                     Not with --effect-lines or --scenario.\n"
            "  --smoke-lod=P      Thin smoke particles larger than P pixels in radius (0 < P <= 4096), fading every other one\n"
            "                     out by 2P and compensating its neighbour (docs/optimizations/03).\n"
            "  --faint-floor=F    Do not draw smoke particles whose opacity is below F (0-1).\n"
            "  --coverage         Count each listed particle's screen coverage and whether it is off screen into the\n"
            "                     counter record (records gain \"coverage\"; counter logs gain its columns).\n"
            "  --counter-log=FILE With --scenario or --profile-tick: write a CSV row per frame at exit (tick, steps, effects\n"
            "                     started and dropped, the frame's live counts and GPU intervals).\n"
            "  --gpu-validation   Debug only: D3D12 GPU-based or Vulkan GPU-assisted validation (slow).\n"
            "  --ui=on|off        Dear ImGui overlay (default on, off with --profile-tick). F1 hides it.\n"
            "  --adapter=NAME     Use the GPU whose name contains NAME (case-insensitive); 'warp' is the D3D12 software rasterizer.\n"
            "  --help             Print usage and exit.\n"
            "Both renderers run the same GPU explosion effect.");
        return 0;
    }

    const int version = SDL_GetVersion();
    SDL_Log("Starting Pyroclast (SDL %d.%d.%d, %s build)",
        SDL_VERSIONNUM_MAJOR(version),
        SDL_VERSIONNUM_MINOR(version),
        SDL_VERSIONNUM_MICRO(version),
        pyroclast::build_configuration);
    SDL_Log("Requested renderer: %s", pyroclast::renderer_name(result.options.renderer));
    const int exit_code = run_application(result.options);
    SDL_Log("Pyroclast shutdown (exit code %d)", exit_code);
    return exit_code;
}
