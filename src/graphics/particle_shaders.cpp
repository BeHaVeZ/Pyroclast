#include "particle_shaders.hpp"

namespace
{
namespace d3d12
{
#include "d3d12/particles_vs.hpp"
#include "d3d12/particles_ps.hpp"
#include "d3d12/particles_compute_cs.hpp"
}
namespace vulkan
{
#include "vulkan/particles_vs.hpp"
#include "vulkan/particles_ps.hpp"
#include "vulkan/particles_compute_cs.hpp"
}
}

namespace pyroclast
{
GraphicsPipelineDescription particle_shaders(Renderer renderer)
{
    if (renderer == Renderer::vulkan)
    {
        return {std::as_bytes(std::span(vulkan::particles_vs)), std::as_bytes(std::span(vulkan::particles_ps)), BlendMode::additive};
    }
    return {std::as_bytes(std::span(d3d12::particles_vs)), std::as_bytes(std::span(d3d12::particles_ps)), BlendMode::additive};
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
