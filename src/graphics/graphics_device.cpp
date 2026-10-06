#include "graphics_device.hpp"
#include "d3d12/d3d12_renderer.hpp"
#include "vulkan/vulkan_renderer.hpp"

#include <SDL3/SDL.h>

namespace pyroclast
{
GraphicsDeviceCreation create_graphics_device(Renderer renderer, SDL_Window* window, bool gpu_timing, bool vsync, bool half_smoke)
{
    if (renderer == Renderer::vulkan)
    {
        auto device = std::make_unique<VulkanRenderer>();
        if (!device->initialize(window, gpu_timing, vsync, half_smoke))
        {
            return {nullptr, "Vulkan initialization failed; see the backend diagnostics."};
        }
        return {std::move(device), {}};
    }
    auto device = std::make_unique<D3D12Renderer>();
    if (!device->initialize(window, gpu_timing, vsync, half_smoke))
    {
        return {nullptr, "Graphics initialization failed; see the backend diagnostics."};
    }
    return {std::move(device), {}};
}
}
