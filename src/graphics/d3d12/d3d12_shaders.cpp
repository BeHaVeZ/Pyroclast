#include "d3d12_shaders.hpp"

namespace
{
#include "d3d12/particles_vs.hpp"
#include "d3d12/particles_ps.hpp"
#include "d3d12/particles_compute_cs.hpp"
#include "d3d12/upscale_vs.hpp"
#include "d3d12/upscale_ps.hpp"
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
    case Shader::upscale_vertex: return std::as_bytes(std::span(upscale_vs));
    case Shader::upscale_pixel: return std::as_bytes(std::span(upscale_ps));
    }
    return {};
}
}
