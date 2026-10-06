#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "command_line.hpp"
#include "frame_baseline.hpp"
#include "../simulation/particle_system.hpp"

#include <cstdio>
#include <memory>

namespace
{
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

int run_event_loop(SDL_Window* window, pyroclast::GraphicsDevice* renderer,
    pyroclast::ParticleSystem& demo)
{
    const auto window_id = SDL_GetWindowID(window);
    pyroclast::FrameBaseline baseline;
    auto previous_time = SDL_GetPerformanceCounter();
    while (true)
    {
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
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
                demo.toggle_emission();
            }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.windowID == window_id && !event.key.repeat)
            {
                if (event.key.key == SDLK_R) demo.reset();
                if (event.key.key == SDLK_P) demo.toggle_pause();
                if (event.key.key == SDLK_N) demo.step();
            }
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                    event.window.windowID == window_id))
            {
                return 0;
            }
        }

        const bool minimized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0;
        if (renderer && !minimized)
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
                if (!renderer->resize(width, height))
                {
                    return 1;
                }
                const double elapsed = static_cast<double>(start - previous_time) /
                    static_cast<double>(SDL_GetPerformanceFrequency());
                previous_time = start;
                const auto result = demo.render(*renderer, static_cast<float>(width) / static_cast<float>(height), elapsed);
                if (result == pyroclast::RenderResult::failed) return 1;
                if (result == pyroclast::RenderResult::submitted)
                {
                    const double milliseconds = 1000.0 * static_cast<double>(SDL_GetPerformanceCounter() - start) /
                        static_cast<double>(SDL_GetPerformanceFrequency());
                    baseline.record(milliseconds);
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

    auto creation = pyroclast::create_graphics_device(options.renderer, window.get());
    if (!creation.error.empty())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", creation.error.c_str());
        return 1;
    }
    pyroclast::ParticleSystem demo;
    if (creation.device && !demo.initialize(*creation.device, options.renderer))
    {
        return 1;
    }
    SDL_SetWindowTitle(window.get(), "Pyroclast - GPU particles | Space: emission | R: reset | P: pause | N: step");
    SDL_Log("GPU particle baseline ready. Space: emission, R: reset, P: pause, N: single step.");
    const int exit_code = run_event_loop(window.get(), creation.device.get(), demo);
    return !creation.device || creation.device->wait_idle() ? exit_code : 1;
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
            "Usage: Pyroclast.exe [--renderer=d3d12|vulkan] [--help]\n"
            "  --renderer=d3d12   Request Direct3D 12 (default).\n"
            "  --renderer=vulkan  Request Vulkan.\n"
            "  --help             Print usage and exit.\n"
            "Both renderers run the same GPU particle emitter.");
        return 0;
    }

    const int version = SDL_GetVersion();
    SDL_Log("Starting Pyroclast (SDL %d.%d.%d)",
        SDL_VERSIONNUM_MAJOR(version),
        SDL_VERSIONNUM_MINOR(version),
        SDL_VERSIONNUM_MICRO(version));
    SDL_Log("Requested renderer: %s", pyroclast::renderer_name(result.options.renderer));
    const int exit_code = run_application(result.options);
    SDL_Log("Pyroclast shutdown (exit code %d)", exit_code);
    return exit_code;
}
