#include "triangle_shaders.hpp"

namespace
{
namespace d3d12
{
#ifdef NDEBUG
#include "d3d12/Release/triangle_vs.hpp"
#include "d3d12/Release/triangle_ps.hpp"
#include "d3d12/Release/colors_compute_cs.hpp"
#else
#include "d3d12/Debug/triangle_vs.hpp"
#include "d3d12/Debug/triangle_ps.hpp"
#include "d3d12/Debug/colors_compute_cs.hpp"
#endif
}
namespace vulkan
{
#ifdef NDEBUG
#include "vulkan/Release/triangle_vs.hpp"
#include "vulkan/Release/triangle_ps.hpp"
#include "vulkan/Release/colors_compute_cs.hpp"
#else
#include "vulkan/Debug/triangle_vs.hpp"
#include "vulkan/Debug/triangle_ps.hpp"
#include "vulkan/Debug/colors_compute_cs.hpp"
#endif
}
}

namespace pyroclast
{
GraphicsPipelineDescription triangle_shaders(Renderer renderer)
{
    if (renderer == Renderer::vulkan)
    {
        return {std::as_bytes(std::span(vulkan::triangle_vs)), std::as_bytes(std::span(vulkan::triangle_ps))};
    }
    return {std::as_bytes(std::span(d3d12::triangle_vs)), std::as_bytes(std::span(d3d12::triangle_ps))};
}

std::span<const std::byte> triangle_compute_shader(Renderer renderer)
{
    if (renderer == Renderer::vulkan)
    {
        return std::as_bytes(std::span(vulkan::colors_compute_cs));
    }
    return std::as_bytes(std::span(d3d12::colors_compute_cs));
}
}
