#include "explosion_renderer.hpp"

#include <cassert>
#include <cmath>

namespace pyroclast
{
namespace
{
constexpr std::array<float, 4> background_color{0.04f, 0.07f, 0.12f, 1.0f};
}

RenderResult render_explosions(GraphicsDevice& device, ParticleSystem& particles, float aspect_ratio, double elapsed_seconds)
{
    assert(std::isfinite(aspect_ratio) && aspect_ratio > 0);
    const auto work = particles.plan(elapsed_seconds);
    const DrawRange ranges[] = {work.smoke, work.emissive};
    const auto result = device.render({particles.particles(), ranges, background_color, &work.dispatch, aspect_ratio});
    if (result == RenderResult::submitted)
    {
        particles.commit(work);
    }
    return result;
}
}
