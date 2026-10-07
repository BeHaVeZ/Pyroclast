#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "command_line.hpp"
#include "explosion_renderer.hpp"
#include "frame_baseline.hpp"
#include "../tools/debug_ui.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
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

void apply(pyroclast::UiCommand command, pyroclast::ParticleSystem& demo)
{
    switch (command)
    {
    case pyroclast::UiCommand::trigger: demo.trigger_explosion(); break;
    case pyroclast::UiCommand::clear: demo.reset(); break;
    case pyroclast::UiCommand::toggle_pause: demo.toggle_pause(); break;
    case pyroclast::UiCommand::step: demo.step(); break;
    }
}

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

int run_event_loop(SDL_Window* window, pyroclast::GraphicsDevice& renderer, pyroclast::ParticleSystem& demo,
    const pyroclast::TestRange& range, pyroclast::ExplosionRenderer& explosions, pyroclast::DebugUi* ui,
    const pyroclast::ApplicationOptions& options)
{
    const auto window_id = SDL_GetWindowID(window);
    auto camera = range.view(options.view);
    FlightControls flight;
    const auto profile_tick = options.profile_tick;
    pyroclast::FrameBaseline baseline({pyroclast::renderer_name(options.renderer), std::string(renderer.adapter_name()),
        demo.explosion_count(), demo.capacity(), profile_tick, options.half_smoke});
    if (ui)
    {
        baseline.disable();
        SDL_Log("Startup sample skipped while the overlay is on; launch with --ui=off to take it.");
    }
    FrameHistory history;
    std::vector<pyroclast::UiCommand> commands;
    auto previous_time = SDL_GetPerformanceCounter();
    std::uint32_t submitted_frames = 0;
    const auto finish = [&options]() {
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
            if (ui && event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F1 && !event.key.repeat) ui->toggle_visible();
            if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_WINDOW_MINIMIZED ||
                event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
                (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED &&
                    (event.window.data1 != 1280 || event.window.data2 != 720)))
            {
                baseline.disable();
            }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.windowID == window_id &&
                event.key.key == SDLK_SPACE && !event.key.repeat)
            {
                demo.trigger_explosion();
            }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.windowID == window_id && !event.key.repeat)
            {
                if (event.key.key == SDLK_R) demo.reset();
                if (event.key.key == SDLK_P) demo.toggle_pause();
                if (event.key.key == SDLK_N) demo.step();
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
                if (width != 1280 || height != 720) baseline.disable();
                const auto start = SDL_GetPerformanceCounter();
                if (!renderer.resize(width, height))
                {
                    return 1;
                }
                const double elapsed = static_cast<double>(start - previous_time) /
                    static_cast<double>(SDL_GetPerformanceFrequency());
                previous_time = start;
                const auto input = flight.input(!ui || !ui->wants_keyboard());
                if (input.move.x != 0 || input.move.y != 0 || input.move.z != 0 || input.look.x != 0 || input.look.y != 0)
                {
                    baseline.disable();
                    pyroclast::fly(camera, input, static_cast<float>(elapsed), flight.speed);
                }
                ImDrawData* overlay = nullptr;
                if (ui)
                {
                    const pyroclast::DiagnosticsSnapshot snapshot{pyroclast::renderer_name(options.renderer), renderer.adapter_name(),
                        pyroclast::build_configuration, width, height, !profile_tick.has_value(),
                        std::span<const float>(history.milliseconds.data(), history.count),
                        history.count < history.milliseconds.size() ? 0 : static_cast<int>(history.next),
                        renderer.completed_gpu_timings(), demo.explosion_count(), demo.capacity(), demo.paused(), options.half_smoke,
                        {camera.position.x, camera.position.y, camera.position.z},
                        camera.yaw * 180 / std::numbers::pi_v<float>, camera.pitch * 180 / std::numbers::pi_v<float>, flight.speed};
                    commands.clear();
                    overlay = ui->build(snapshot, commands);
                    for (const auto command : commands) apply(command, demo);
                }
                const bool capture = !options.capture_path.empty() && !demo.has_pending_steps();
                const auto result = explosions.render(renderer, demo, range, camera, width, height, elapsed, capture, overlay);
                if (result == pyroclast::RenderResult::failed) return 1;
                if (result == pyroclast::RenderResult::submitted)
                {
                    history.add(1000.0 * elapsed);
                    const double milliseconds = 1000.0 * static_cast<double>(SDL_GetPerformanceCounter() - start) /
                        static_cast<double>(SDL_GetPerformanceFrequency());
                    if (!profile_tick || !demo.has_pending_steps())
                        baseline.record(milliseconds, renderer.completed_gpu_timings());
                    if (capture) return save_capture(renderer, options.capture_path);
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
    std::unique_ptr<pyroclast::DebugUi> ui;
    if (options.ui.value_or(!options.profile_tick))
    {
        ui = pyroclast::DebugUi::create(window.get(), options.renderer);
        if (!ui) return 1;
    }
    const pyroclast::GraphicsDeviceOptions device_options{
        .gpu_timing = options.gpu_timing || options.profile_tick.has_value() || ui != nullptr,
        .vsync = !options.profile_tick.has_value(),
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
    SDL_Log("Smoke resolution: %s.", options.half_smoke ? "half" : "full");
    pyroclast::TestRange range;
    pyroclast::ParticleSystem demo;
    pyroclast::ExplosionRenderer explosions;
    if (!range.initialize(device) || !demo.initialize(device, range.terrain(), range.heights(), options.explosions) ||
        !explosions.initialize(device, options.half_smoke))
    {
        return 1;
    }
    if (options.profile_tick)
    {
        demo.prepare_profile(*options.profile_tick);
        SDL_Log("Fixed-state profile: tick=%u; VSync off; timestamps on; zero integration steps after preparation.", *options.profile_tick);
    }
    SDL_SetWindowTitle(window.get(), ui ? "Pyroclast - Explosion | WASD/QE, right mouse: fly | Space: trigger | R: clear | P: pause | N: step | F1: overlay" :
        "Pyroclast - Explosion | WASD/QE, right mouse: fly | Space: trigger | R: clear | P: pause | N: step");
    SDL_Log("Explosion demo ready. Space: trigger, R: clear, P: pause, N: single step; WASD/QE with the right mouse button flies the camera.");
    const int exit_code = run_event_loop(window.get(), device, demo, range, explosions, ui.get(), options);
    return device.wait_idle() ? exit_code : 1;
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
            "                     [--capture=FILE.png] [--exit-after=N] [--view=overview|ground|hill|aerial] [--help]\n"
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
