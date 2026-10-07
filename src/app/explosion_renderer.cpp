#include "explosion_renderer.hpp"

#include <cassert>
#include <cmath>

namespace pyroclast
{
namespace
{
constexpr std::array<float, 4> background_color{0.04f, 0.07f, 0.12f, 1.0f};
}

bool ExplosionRenderer::initialize(GraphicsDevice& device, bool half_resolution_smoke)
{
    if (!half_resolution_smoke)
    {
        return true;
    }
    smoke_target_ = device.create_render_target({2, background_color});
    return smoke_target_ != nullptr;
}

RenderResult ExplosionRenderer::render(GraphicsDevice& device, ParticleSystem& particles, float aspect_ratio, double elapsed_seconds,
    bool capture)
{
    assert(std::isfinite(aspect_ratio) && aspect_ratio > 0);
    const auto work = particles.plan(elapsed_seconds);
    const DrawRange layered[] = {work.smoke, work.emissive};
    const Pass full_resolution[] = {{nullptr, nullptr, layered}};
    const Pass half_resolution[] = {
        {smoke_target_.get(), nullptr, {&work.smoke, 1}},
        {nullptr, smoke_target_.get(), {&work.emissive, 1}}};
    const std::span<const Pass> passes = smoke_target_ ? std::span<const Pass>(half_resolution) : std::span<const Pass>(full_resolution);
    const auto result = device.render({particles.particles(), passes, background_color, &work.dispatch, aspect_ratio, capture});
    if (result == RenderResult::submitted)
    {
        particles.commit(work);
    }
    return result;
}
}
