#include "explosion_renderer.hpp"

#include <algorithm>
#include <cassert>
#include <numeric>

namespace pyroclast
{
bool ExplosionRenderer::initialize(GraphicsDevice& device, bool half_resolution_smoke, TargetFormat scene_format)
{
    scene_ = device.create_render_target({1, {}, scene_format});
    depth_ = device.create_render_target({1, {}, TargetFormat::depth, 0.0f});
    present_ = device.create_graphics_pipeline({Shader::tonemap_vertex, Shader::tonemap_pixel, BlendMode::opaque});
    if (!scene_ || !depth_ || !present_)
    {
        return false;
    }
    if (!half_resolution_smoke)
    {
        return true;
    }
    smoke_target_ = device.create_render_target({2, {0, 0, 0, 0}, smoke_format});
    composite_ = device.create_graphics_pipeline(
        {Shader::composite_vertex, Shader::composite_pixel, BlendMode::premultiplied_alpha, DepthMode::ignore, scene_format});
    return smoke_target_ && composite_;
}

RenderResult ExplosionRenderer::render(GraphicsDevice& device, ParticleSystem& particles, const TestRange& range,
    const Camera& camera, int width, int height, double elapsed_seconds, bool capture, ImDrawData* overlay)
{
    assert(width > 0 && height > 0);
    const auto work = particles.plan(elapsed_seconds, smoke_target_ ? smoke_target_->description().divisor : 1);

    FrameConstants constants{};
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    constants.view_projection = camera.projection(w / h) * camera.view();
    constants.inverse_view_projection = inverse(constants.view_projection);
    constants.camera_position = {camera.position.x, camera.position.y, camera.position.z, camera.near_plane};
    const auto right = camera.right();
    const auto up = camera.up();
    constants.camera_right = {right.x, right.y, right.z, 0};
    constants.camera_up = {up.x, up.y, up.z, 0};
    constants.viewport = {w, h, 1 / w, 1 / h};
    constants.projection = {camera.projection(w / h).columns[1].y * h * 0.5f, 0, 0, 0};
    TestRange::light(constants);
    const auto origins = particles.effect_origins();
    std::copy(origins.begin(), origins.end(), constants.effect_origins.begin());
    std::array<std::uint32_t, maximum_explosions> order{};
    const auto effects = order.begin() + static_cast<std::ptrdiff_t>(origins.size());
    std::iota(order.begin(), effects, 0u);
    const auto distance = [&origins, &camera](std::uint32_t effect) {
        const auto offset = float3{origins[effect].x, origins[effect].y, origins[effect].z} - camera.position;
        return dot(offset, offset);
    };
    std::stable_sort(order.begin(), effects, [&distance](std::uint32_t a, std::uint32_t b) { return distance(a) > distance(b); });
    for (std::size_t rank = 0; rank < order.size(); ++rank) constants.effect_order[rank / 4][rank % 4] = order[rank];

    const auto opaque = range.draws();
    const Draw present{*present_, {}, {}, 3, 1, "Tonemap"};
    const Pass scene{scene_.get(), {}, opaque, "Scene", false, depth_.get(), true};
    const Pass to_backbuffer{nullptr, {scene_.get(), nullptr}, {&present, 1}, "Present"};
    const auto submit = [&](std::span<const Pass> passes) {
        return device.render({{&work.dispatch, 1}, passes, {}, std::as_bytes(std::span(&constants, 1)), capture, overlay});
    };
    RenderResult result;
    if (!smoke_target_)
    {
        const Pass passes[] = {scene, {scene_.get(), {nullptr, depth_.get()}, {&work.particles, 1}, "Particles", true, depth_.get(), false},
            to_backbuffer};
        result = submit(passes);
    }
    else
    {
        const Draw composite{*composite_, {}, {}, 3, 1, "Smoke composite"};
        const Draw light_over_smoke[] = {composite, work.light};
        const Pass passes[] = {scene,
            {smoke_target_.get(), {nullptr, depth_.get()}, {&work.offscreen_smoke, 1}, "Half-resolution smoke"},
            {scene_.get(), {smoke_target_.get(), depth_.get()}, light_over_smoke, "Composite and light", true, depth_.get(), false},
            to_backbuffer};
        result = submit(passes);
    }
    if (result == RenderResult::submitted)
    {
        particles.commit(work);
    }
    return result;
}
}
