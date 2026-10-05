#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "command_line.hpp"
#include "../graphics/d3d12/d3d12_renderer.hpp"

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

int run_event_loop(SDL_Window* window, pyroclast::D3D12Renderer* renderer)
{
    const auto window_id = SDL_GetWindowID(window);
    while (true)
    {
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
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
                if (!renderer->resize(width, height) || !renderer->render())
                {
                    return 1;
                }
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
        SDL_CreateWindow("Pyroclast", 1280, 720, SDL_WINDOW_RESIZABLE),
        &SDL_DestroyWindow);
    if (!window)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Window creation failed: %s", SDL_GetError());
        return 1;
    }

    if (options.renderer == pyroclast::Renderer::vulkan)
    {
        SDL_Log("Vulkan is not implemented yet; running the SDL window shell.");
        return run_event_loop(window.get(), nullptr);
    }

    pyroclast::D3D12Renderer renderer;
    if (!renderer.initialize(window.get()))
    {
        return 1;
    }
    const int exit_code = run_event_loop(window.get(), &renderer);
    return renderer.wait_idle() ? exit_code : 1;
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
            "D3D12 draws a triangle; Vulkan currently runs the SDL window shell.");
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
