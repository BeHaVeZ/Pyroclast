#include "graphics_device.hpp"
#include "d3d12/d3d12_renderer.hpp"
#include "vulkan/vulkan_renderer.hpp"

#include <SDL3/SDL.h>

namespace pyroclast
{
GraphicsDeviceCreation create_graphics_device(Renderer renderer, SDL_Window* window, const GraphicsDeviceOptions& options)
{
    if (renderer == Renderer::vulkan)
    {
        auto device = std::make_unique<VulkanRenderer>();
        if (!device->initialize(window, options))
        {
            return {nullptr, "Vulkan initialization failed; see the backend diagnostics."};
        }
        return {std::move(device), {}};
    }
    auto device = std::make_unique<D3D12Renderer>();
    if (!device->initialize(window, options))
    {
        return {nullptr, "Graphics initialization failed; see the backend diagnostics."};
    }
    return {std::move(device), {}};
}
}
