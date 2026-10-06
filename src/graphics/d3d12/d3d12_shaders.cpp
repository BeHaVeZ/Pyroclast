#include "d3d12_shaders.hpp"

namespace
{
#include "d3d12/particles_vs.hpp"
#include "d3d12/particles_ps.hpp"
#include "d3d12/particles_compute_cs.hpp"
#include "d3d12/smoke_composite_vs.hpp"
#include "d3d12/smoke_composite_ps.hpp"
}

namespace pyroclast
{
std::span<const std::byte> d3d12_shader(Shader shader)
{
    switch (shader)
    {
    case Shader::particle_vertex: return std::as_bytes(std::span(particles_vs));
    case Shader::particle_pixel: return std::as_bytes(std::span(particles_ps));
    case Shader::particle_simulation: return std::as_bytes(std::span(particles_compute_cs));
    case Shader::smoke_composite_vertex: return std::as_bytes(std::span(smoke_composite_vs));
    case Shader::smoke_composite_pixel: return std::as_bytes(std::span(smoke_composite_ps));
    }
    return {};
}
}
