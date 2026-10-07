#include "test_range.hpp"

#include <SDL3/SDL.h>

#include <cassert>

namespace pyroclast
{
bool TestRange::initialize(GraphicsDevice& device, TargetFormat scene)
{
    const auto heights = terrain_.heights();
    heights_ = device.create_buffer(static_cast<std::uint32_t>(heights.size_bytes()), std::as_bytes(heights));
    terrain_pipeline_ = device.create_graphics_pipeline(
        {Shader::terrain_vertex, Shader::terrain_pixel, BlendMode::opaque, DepthMode::test_write, scene});
    sky_pipeline_ = device.create_graphics_pipeline({Shader::sky_vertex, Shader::sky_pixel, BlendMode::opaque, DepthMode::test, scene});
    if (!heights_ || !terrain_pipeline_ || !sky_pipeline_)
    {
        return false;
    }
    SDL_Log("Test range ready: %u x %u height samples, %.0f m across.", Terrain::resolution, Terrain::resolution,
        2.0 * Terrain::half_extent);
    return true;
}

std::array<Draw, 2> TestRange::draws() const
{
    assert(heights_ && terrain_pipeline_ && sky_pipeline_);
    Bindings terrain{};
    terrain.read[0] = heights_.get();
    constexpr auto cells = Terrain::resolution - 1;
    return {Draw{*terrain_pipeline_, terrain, {}, cells * cells * 6, 1, "Terrain"},
        Draw{*sky_pipeline_, {}, {}, 3, 1, "Sky"}};
}

void TestRange::light(FrameConstants& constants)
{
    const auto sun = normalize(float3{-0.45f, 0.55f, -0.35f});
    constants.sun_direction = {sun.x, sun.y, sun.z, 0};
    constants.sun_color = {2.6f, 2.4f, 2.1f, 0};
    constants.sky_color = {0.045f, 0.14f, 0.48f, 0};
    constants.horizon_color = {0.34f, 0.46f, 0.62f, 0};
    constants.ground_color = {0.064f, 0.051f, 0.036f, 0};
    constants.fog = {0.36f, 0.46f, 0.6f, 0.0035f};
    constants.exposure = {exposure, 0, 0, 0};
}

Camera TestRange::view(RangeView name) const
{
    Camera camera;
    switch (name)
    {
    case RangeView::overview:
        camera.position = {0, 18, 78};
        camera.look_at({0, 6, 0});
        break;
    case RangeView::ground:
        camera.position = {0, 1.7f, 58};
        camera.look_at({6, 5, 0});
        break;
    case RangeView::hill:
        camera.position = {190, terrain_.height(190, 0) + 2, 0};
        camera.look_at({0, 6, 0});
        break;
    case RangeView::aerial:
        camera.position = {0, 140, 120};
        camera.look_at({0, 0, 0});
        break;
    }
    return camera;
}
}
