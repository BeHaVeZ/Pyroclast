#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "command_line.hpp"
#include "explosion_renderer.hpp"
#include "frame_baseline.hpp"
#include "frame_statistics.hpp"
#include "../simulation/explosion_scenarios.hpp"
#include "../tools/debug_ui.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
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
};

struct Sandbox
{
    pyroclast::TestRange range;
    pyroclast::ParticleSystem particles;
    pyroclast::ExplosionSystem effects{pyroclast::ParticleSystem::effect_slots};
    pyroclast::ExplosionRenderer renderer;
    std::vector<pyroclast::ExplosionEvent> group;
    bool debug_lines = false;
    std::uint32_t next_seed = 100000;
    pyroclast::ExplosionPreset click_preset = pyroclast::ExplosionPreset::small_he;
    std::uint32_t layer_mask = pyroclast::particle_layout::all_layers_mask;
    std::optional<pyroclast::ParticleCounters> counters;
    std::uint64_t counter_sample = 0;

    std::optional<pyroclast::ParticleCounters> poll_counters(const pyroclast::GraphicsDevice& device)
    {
        const auto readback = device.completed_readback();
        if (!readback || readback->sample_id == counter_sample) return std::nullopt;
        counter_sample = readback->sample_id;
        const auto fresh = pyroclast::ParticleSystem::counters(*readback);
        if (fresh) counters = fresh;
        return fresh;
    }

    void start_group()
    {
        effects.clear();
        for (const auto& event : group) effects.submit(event);
        particles.discard_pending();
    }
    void trigger_group()
    {
        start_group();
        SDL_Log("Explosion group triggered: %zu effects, %zu particles; replaces the previous group.", group.size(),
            group.size() * pyroclast::ParticleSystem::slab_records);
    }
    void clear()
    {
        effects.clear();
        particles.discard_pending();
        SDL_Log("Particle pool cleared.");
    }
    void spawn(const pyroclast::Ray& ray)
    {
        const auto hit = range.terrain().raycast(ray, std::numeric_limits<float>::infinity());
        if (!hit)
        {
            SDL_Log("No terrain under the cursor.");
            return;
        }
        const auto normal = range.terrain().normal(hit->x, hit->z);
        const auto surface = pyroclast::Terrain::surface(hit->x, hit->z);
        effects.submit({*hit + normal * 0.5f, normal, next_seed++, click_preset, surface});
        const auto preset = pyroclast::preset_name(click_preset);
        const auto ground = pyroclast::surface_name(surface);
        SDL_Log("Explosion at (%.1f, %.1f, %.1f): %.*s on %.*s.", hit->x, hit->y, hit->z, static_cast<int>(preset.size()), preset.data(),
            static_cast<int>(ground.size()), ground.data());
    }
};

void apply(pyroclast::UiCommand command, Sandbox& sandbox)
{
    using Kind = pyroclast::UiCommand::Kind;
    switch (command.kind)
    {
    case Kind::trigger: sandbox.trigger_group(); break;
    case Kind::clear: sandbox.clear(); break;
    case Kind::toggle_pause: sandbox.particles.toggle_pause(); break;
    case Kind::step: sandbox.particles.step(); break;
    case Kind::toggle_debug_lines: sandbox.debug_lines = !sandbox.debug_lines; break;
    case Kind::select_preset:
        if (command.value < pyroclast::preset_count) sandbox.click_preset = static_cast<pyroclast::ExplosionPreset>(command.value);
        break;
    case Kind::toggle_layer: sandbox.layer_mask ^= 1u << (command.value & 31); break;
    }
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
        }
        peak_effects_ = std::max(peak_effects_, static_cast<std::uint32_t>(sandbox.effects.active().size()));
    }
    void count(const pyroclast::ParticleCounters& counters)
    {
        ++counter_samples_;
        peak_alive_ = std::max(peak_alive_, counters.alive);
        alive_sum_ += counters.alive;
        const std::uint32_t words[] = {counters.tick, counters.alive, counters.smoke, counters.light};
        for (const auto word : words)
        {
            for (int byte = 0; byte < 4; ++byte)
            {
                checksum_ = (checksum_ ^ ((word >> (8 * byte)) & 0xff)) * 0x100000001b3ull;
            }
        }
    }
    int report(const Sandbox& sandbox, const pyroclast::ApplicationOptions& options, std::string_view gpu_name, int width, int height,
        bool overlay)
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
        std::printf("{\"scenario\":\"%.*s\",\"valid\":%s,\"build\":\"%s\",\"renderer\":\"%s\",\"gpu\":%s,\"width\":%d,\"height\":%d,"
            "\"smoke_resolution\":\"%s\",\"scene_format\":\"%s\",\"view\":\"%s\",\"overlay\":%s,\"effect_lines\":%s,\"gpu_regions\":%s,"
            "\"preset\":\"%.*s\",\"layer_mask\":%u,\"ticks\":%u,\"frames\":%zu,\"effects_started\":%llu,\"effects_dropped\":%llu,\"peak_effects\":%u,"
            "\"particles\":{\"samples\":%zu,\"peak_alive\":%u,\"mean_alive\":%.3f,\"checksum\":\"%016llx\"},\"cpu_ms\":%s,"
            "\"gpu_ms\":{\"samples\":%zu,\"draw\":%s,\"compute\":%s}}\n",
            static_cast<int>(scenario_.name.size()), scenario_.name.data(), valid_ ? "true" : "false", pyroclast::build_configuration,
            pyroclast::renderer_name(options.renderer), gpu.c_str(), width, height, options.half_smoke ? "half" : "full",
            pyroclast::scene_format_name(options.scene_format), pyroclast::view_name(options.view), overlay ? "true" : "false",
            sandbox.debug_lines ? "true" : "false", options.gpu_regions ? "true" : "false",
            static_cast<int>(pyroclast::preset_name(options.preset).size()), pyroclast::preset_name(options.preset).data(),
            sandbox.layer_mask, scenario_.duration_ticks, frames,
            static_cast<unsigned long long>(sandbox.effects.started()), static_cast<unsigned long long>(sandbox.effects.dropped()),
            peak_effects_, counter_samples_, peak_alive_, mean_alive, static_cast<unsigned long long>(checksum_), cpu.c_str(),
            draw_ms_.size(), draw.c_str(), compute.c_str());
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
    std::uint32_t peak_effects_ = 0;
    std::size_t counter_samples_ = 0;
    std::uint32_t peak_alive_ = 0;
    std::uint64_t alive_sum_ = 0;
    std::uint64_t checksum_ = 0xcbf29ce484222325ull;
    bool valid_ = true;
};

struct FlightControls
{
    bool looking = false;
    pyroclast::float2 look;
    float speed = 15;

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

int run_event_loop(SDL_Window* window, pyroclast::GraphicsDevice& renderer, Sandbox& sandbox, pyroclast::DebugUi* ui,
    const pyroclast::ApplicationOptions& options, std::optional<ScenarioRun> scenario)
{
    const auto window_id = SDL_GetWindowID(window);
    auto& demo = sandbox.particles;
    auto camera = sandbox.range.view(options.view);
    FlightControls flight;
    const auto profile_tick = options.profile_tick;
    pyroclast::FrameBaseline baseline({pyroclast::renderer_name(options.renderer), std::string(renderer.adapter_name()),
        static_cast<std::uint32_t>(sandbox.group.size()), pyroclast::ParticleSystem::capacity, profile_tick, options.half_smoke,
        pyroclast::view_name(options.view), pyroclast::scene_format_name(options.scene_format), std::string(pyroclast::preset_name(options.preset)),
        options.layer_mask});
    if (scenario) baseline.disable();
    if (ui)
    {
        baseline.disable();
        SDL_Log("Startup sample skipped while the overlay is on; launch with --ui=off to take it.");
    }
    FrameHistory history;
    std::vector<pyroclast::UiCommand> commands;
    auto previous_time = SDL_GetPerformanceCounter();
    std::uint32_t submitted_frames = 0;
    const auto finish = [&options, &scenario]() {
        if (scenario)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Exited before scenario %s finished; no summary.", options.scenario.c_str());
            return 1;
        }
        if (options.capture_path.empty()) return 0;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Exited before the capture at tick %u was drawn.", *options.profile_tick);
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
            if (scenario) continue;
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
                flight.speed = std::clamp(flight.speed * std::pow(1.25f, event.wheel.y), 1.0f, 400.0f);
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.windowID == window_id &&
                event.button.button == SDL_BUTTON_LEFT && !flight.looking)
            {
                int window_width = 0;
                int window_height = 0;
                if (SDL_GetWindowSize(window, &window_width, &window_height) && window_width > 0 && window_height > 0)
                {
                    const float w = static_cast<float>(window_width);
                    const float h = static_cast<float>(window_height);
                    const auto ndc = pyroclast::window_to_ndc(event.button.x, event.button.y, w, h);
                    sandbox.spawn(camera.ray_through(ndc.x, ndc.y, w / h));
                    baseline.disable();
                }
            }
            if (event.type == SDL_EVENT_KEY_DOWN) baseline.disable();
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.windowID == window_id && !event.key.repeat)
            {
                using Kind = pyroclast::UiCommand::Kind;
                if (event.key.key == SDLK_SPACE) apply({Kind::trigger}, sandbox);
                if (event.key.key == SDLK_R) apply({Kind::clear}, sandbox);
                if (event.key.key == SDLK_P) apply({Kind::toggle_pause}, sandbox);
                if (event.key.key == SDLK_N) apply({Kind::step}, sandbox);
                if (event.key.key == SDLK_L) apply({Kind::toggle_debug_lines}, sandbox);
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
                const auto input = flight.input(!scenario && (!ui || !ui->wants_keyboard()));
                if (input.move.x != 0 || input.move.y != 0 || input.move.z != 0 || input.look.x != 0 || input.look.y != 0)
                {
                    baseline.disable();
                    pyroclast::fly(camera, input, static_cast<float>(elapsed), flight.speed);
                }
                ImDrawData* overlay = nullptr;
                if (ui)
                {
                    const pyroclast::DiagnosticsSnapshot snapshot{pyroclast::renderer_name(options.renderer), renderer.adapter_name(),
                        pyroclast::build_configuration, width, height, !profile_tick.has_value() && !scenario,
                        std::span<const float>(history.milliseconds.data(), history.count),
                        history.count < history.milliseconds.size() ? 0 : static_cast<int>(history.next),
                        renderer.completed_gpu_timings(), static_cast<std::uint32_t>(sandbox.group.size()),
                        static_cast<std::uint32_t>(sandbox.effects.active().size()), sandbox.effects.capacity(),
                        sandbox.effects.started(), sandbox.effects.dropped(), demo.tick(),
                        sandbox.effects.active().empty() ? std::nullopt : sandbox.counters, sandbox.debug_lines, demo.paused(),
                        options.half_smoke,
                        pyroclast::scene_format_name(options.scene_format), pyroclast::TestRange::exposure,
                        {camera.position.x, camera.position.y, camera.position.z},
                        camera.yaw * 180 / std::numbers::pi_v<float>, camera.pitch * 180 / std::numbers::pi_v<float>, flight.speed,
                        sandbox.click_preset, sandbox.layer_mask, sandbox.renderer.lights()};
                    commands.clear();
                    overlay = ui->build(snapshot, commands);
                    if (!scenario) for (const auto command : commands) apply(command, sandbox);
                }
                if (scenario) scenario->submit_due(sandbox);
                const bool capture = !options.capture_path.empty() && !demo.has_pending_steps();
                const auto result = sandbox.renderer.render(renderer, demo, sandbox.effects, sandbox.range, camera, width, height, elapsed,
                    {capture, overlay, sandbox.debug_lines, sandbox.layer_mask});
                if (result == pyroclast::RenderResult::failed) return 1;
                const auto counters = sandbox.poll_counters(renderer);
                if (scenario && counters) scenario->count(*counters);
                if (result == pyroclast::RenderResult::submitted)
                {
                    history.add(1000.0 * elapsed);
                    const double milliseconds = 1000.0 * static_cast<double>(SDL_GetPerformanceCounter() - start) /
                        static_cast<double>(SDL_GetPerformanceFrequency());
                    if (!profile_tick && baseline.enabled() && sandbox.effects.active().size() != sandbox.group.size())
                    {
                        baseline.disable();
                        SDL_Log("Startup sample stopped: the group retired before the sample completed; use --profile-tick or --scenario.");
                    }
                    if (!profile_tick || !demo.has_pending_steps())
                        baseline.record(milliseconds, renderer.completed_gpu_timings(), sandbox.effects.active().empty() ?
                            std::optional<std::uint32_t>(0) : sandbox.counters ? std::optional(sandbox.counters->alive) : std::nullopt);
                    if (capture) return save_capture(renderer, options.capture_path);
                    if (scenario)
                    {
                        scenario->record(milliseconds, renderer.completed_gpu_timings(), sandbox);
                        if (scenario->finished(sandbox))
                        {
                            return scenario->report(sandbox, options, renderer.adapter_name(), width, height, ui != nullptr);
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
    SDL_Log("Smoke resolution: %s; scene target: %s.", options.half_smoke ? "half" : "full",
        pyroclast::scene_format_name(options.scene_format));
    const auto scene_format = options.scene_format == pyroclast::SceneFormat::rgba16f ? pyroclast::TargetFormat::rgba16_float :
        options.scene_format == pyroclast::SceneFormat::r11g11b10f ? pyroclast::TargetFormat::r11g11b10_float : pyroclast::TargetFormat::color;
    auto sandbox = std::make_unique<Sandbox>();
    if (!sandbox->range.initialize(device, scene_format) ||
        !sandbox->particles.initialize(device, sandbox->range.heights(), {scene_format, pyroclast::ExplosionRenderer::smoke_format},
            pyroclast::effect_classes()) ||
        !sandbox->renderer.initialize(device, options.half_smoke, scene_format))
    {
        return 1;
    }
    sandbox->debug_lines = options.effect_lines;
    sandbox->click_preset = options.preset;
    sandbox->layer_mask = options.layer_mask;
    const auto preset = pyroclast::preset_name(options.preset);
    SDL_Log("Preset: %.*s; layer mask 0x%02x.", static_cast<int>(preset.size()), preset.data(), options.layer_mask);
    std::optional<ScenarioRun> scenario;
    if (!options.scenario.empty())
    {
        auto script = pyroclast::make_scenario(options.scenario, sandbox->range.terrain(), options.preset);
        if (!script) return 1;
        SDL_Log("Scenario %s: %zu events over %u ticks, one tick per frame; VSync off; timestamps on.", options.scenario.c_str(),
            script->events.size(), script->duration_ticks);
        scenario.emplace(std::move(*script));
        sandbox->particles.set_fixed_ticks(true);
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
    SDL_SetWindowTitle(window.get(), ui ?
        "Pyroclast | WASD/QE, right mouse: fly | left click: explosion | Space: group | R: clear | P: pause | N: step | L: lines | F1: overlay" :
        "Pyroclast | WASD/QE, right mouse: fly | left click: explosion | Space: group | R: clear | P: pause | N: step | L: lines");
    SDL_Log("Explosion demo ready. Space: trigger, R: clear, P: pause, N: single step, L: effect lines; left click places an explosion; "
        "WASD/QE with the right mouse button flies the camera.");
    const int exit_code = run_event_loop(window.get(), device, *sandbox, ui.get(), options, std::move(scenario));
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
            "                     [--smoke-resolution=full|half] [--ui=on|off] [--gpu-validation]\n"
            "                     [--capture=FILE.png] [--exit-after=N] [--view=overview|ground|hill|aerial]\n"
            "                     [--scene-format=rgba16f|r11g11b10f|rgba8] [--scenario=NAME] [--effect-lines] [--help]\n"
            "  --renderer=d3d12   Request Direct3D 12 (default).\n"
            "  --renderer=vulkan  Request Vulkan.\n"
            "  --explosions=N     Simultaneous explosion group (1, 10, 50, 100; default 1).\n"
            "  --profile-tick=N   Freeze at tick 0-480; enable timestamps and disable VSync.\n"
            "  --smoke-resolution=full|half  Select smoke rendering resolution (default full).\n"
            "  --gpu-timing       Enable GPU timestamp measurements.\n"
            "  --gpu-regions      Also time every dispatch, pass and the overlay (serializes Vulkan GPU work).\n"
            "  --capture=FILE.png Write the first frame drawn at the profile tick, then exit (requires --profile-tick).\n"
            "  --exit-after=N     Exit after N submitted frames.\n"
            "  --view=NAME        Start the camera at a named pose: overview (default), ground, hill or aerial.\n"
            "  --scene-format=F   Scene target: rgba16f (default), r11g11b10f, or rgba8 (a cost reference; bands).\n"
            "  --scenario=NAME    Scripted run, one tick per frame, then a JSON summary: single, grid-10, grid-50,\n"
            "                     grid-100, barrage (80 impacts walking across the range) or presets (all seven in a row).\n"
            "  --preset=NAME      small-impact, small-he (default), artillery-he, heavy, fuel-fire, dust-blast or stress-test,\n"
            "                     for the startup group, profiles, scenarios and the first click.\n"
            "  --disable=LIST     Hide layers or the lights: comma-separated dust, smoke, plume, flash, fireball, sparks,\n"
            "                     debris, lights.\n"
            "  --effect-lines     Start with each effect's origin, normal and reach drawn as lines (L toggles).\n"
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
