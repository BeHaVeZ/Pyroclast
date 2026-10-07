#include "explosion_renderer.hpp"

#include <cassert>
#include <cmath>

namespace pyroclast
{
namespace
{
constexpr std::array<float, 4> background_color{0.04f, 0.07f, 0.12f, 1.0f};

struct FrameConstants
{
    float aspect_ratio;
};
}

bool ExplosionRenderer::initialize(GraphicsDevice& device, bool half_resolution_smoke)
{
    if (!half_resolution_smoke)
    {
        return true;
    }
    smoke_target_ = device.create_render_target({2, background_color});
    composite_ = device.create_graphics_pipeline({Shader::composite_vertex, Shader::composite_pixel, BlendMode::opaque});
    return smoke_target_ && composite_;
}

RenderResult ExplosionRenderer::render(GraphicsDevice& device, ParticleSystem& particles, float aspect_ratio, double elapsed_seconds,
    bool capture, ImDrawData* overlay)
{
    assert(std::isfinite(aspect_ratio) && aspect_ratio > 0);
    const auto work = particles.plan(elapsed_seconds);
    const FrameConstants constants{aspect_ratio};
    const auto submit = [&](std::span<const Pass> passes) {
        return device.render({{&work.dispatch, 1}, passes, background_color, std::as_bytes(std::span(&constants, 1)), capture, overlay});
    };
    RenderResult result;
    if (!smoke_target_)
    {
        const Draw layered[] = {work.smoke, work.emissive};
        const Pass passes[] = {{nullptr, {}, layered, "Particles"}};
        result = submit(passes);
    }
    else
    {
        const Draw composite{*composite_, {}, {}, 3, 1, "Smoke composite"};
        const Draw light_over_smoke[] = {composite, work.emissive};
        const Pass passes[] = {
            {smoke_target_.get(), {}, {&work.smoke, 1}, "Half-resolution smoke"},
            {nullptr, {smoke_target_.get(), nullptr}, light_over_smoke, "Composite and light"}};
        result = submit(passes);
    }
    if (result == RenderResult::submitted)
    {
        particles.commit(work);
    }
    return result;
}
}
