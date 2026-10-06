#include "particle_shaders.hpp"

namespace
{
namespace d3d12
{
#ifdef NDEBUG
#include "d3d12/Release/particles_vs.hpp"
#include "d3d12/Release/particles_ps.hpp"
#include "d3d12/Release/particles_compute_cs.hpp"
#else
#include "d3d12/Debug/particles_vs.hpp"
#include "d3d12/Debug/particles_ps.hpp"
#include "d3d12/Debug/particles_compute_cs.hpp"
#endif
}
namespace vulkan
{
#ifdef NDEBUG
#include "vulkan/Release/particles_vs.hpp"
#include "vulkan/Release/particles_ps.hpp"
#include "vulkan/Release/particles_compute_cs.hpp"
#else
#include "vulkan/Debug/particles_vs.hpp"
#include "vulkan/Debug/particles_ps.hpp"
#include "vulkan/Debug/particles_compute_cs.hpp"
#endif
}
}

namespace pyroclast
{
GraphicsPipelineDescription particle_shaders(Renderer renderer)
{
    if (renderer == Renderer::vulkan)
    {
        return {std::as_bytes(std::span(vulkan::particles_vs)), std::as_bytes(std::span(vulkan::particles_ps)), VertexLayout::particle, BlendMode::additive};
    }
    return {std::as_bytes(std::span(d3d12::particles_vs)), std::as_bytes(std::span(d3d12::particles_ps)), VertexLayout::particle, BlendMode::additive};
}

std::span<const std::byte> particle_compute_shader(Renderer renderer)
{
    if (renderer == Renderer::vulkan)
    {
        return std::as_bytes(std::span(vulkan::particles_compute_cs));
    }
    return std::as_bytes(std::span(d3d12::particles_compute_cs));
}
}
