#include "test_range.hpp"

#include <SDL3/SDL.h>

#include <cassert>

namespace pyroclast
{
bool TestRange::initialize(GraphicsDevice& device)
{
    const auto heights = terrain_.heights();
    heights_ = device.create_buffer(static_cast<std::uint32_t>(heights.size_bytes()), std::as_bytes(heights));
    terrain_pipeline_ = device.create_graphics_pipeline(
        {Shader::terrain_vertex, Shader::terrain_pixel, BlendMode::opaque, DepthMode::test_write});
    sky_pipeline_ = device.create_graphics_pipeline({Shader::sky_vertex, Shader::sky_pixel, BlendMode::opaque, DepthMode::test});
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
    constants.sun_color = {1.25f, 1.15f, 1.0f, 0};
    constants.sky_color = {0.32f, 0.50f, 0.78f, 0};
    constants.horizon_color = {0.72f, 0.79f, 0.86f, 0};
    constants.ground_color = {0.28f, 0.25f, 0.21f, 0};
    constants.fog = {0.70f, 0.76f, 0.83f, 0.0035f};
}

Camera TestRange::overview()
{
    Camera camera;
    camera.position = {0, 18, 78};
    camera.look_at({0, 6, 0});
    return camera;
}
}
