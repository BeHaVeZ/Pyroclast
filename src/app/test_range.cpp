#include "test_range.hpp"
#include "prop_mesh.hpp"

#include <SDL3/SDL.h>

#include <cassert>
#include <span>

namespace pyroclast
{
bool TestRange::initialize(GraphicsDevice& device, TargetFormat scene)
{
    const auto bytes = environment_.bytes();
    environment_buffer_ = device.create_buffer(static_cast<std::uint32_t>(bytes.size()), bytes);
    const auto mesh = build_prop_mesh(environment_);
    prop_vertex_count_ = static_cast<std::uint32_t>(mesh.size());
    const auto mesh_bytes = std::as_bytes(std::span(mesh));
    prop_vertices_ = device.create_buffer(static_cast<std::uint32_t>(mesh_bytes.size()), mesh_bytes);
    props_pipeline_ = device.create_graphics_pipeline(
        {Shader::props_vertex, Shader::props_pixel, BlendMode::opaque, DepthMode::test_write, scene});
    terrain_pipeline_ = device.create_graphics_pipeline(
        {Shader::terrain_vertex, Shader::terrain_pixel, BlendMode::opaque, DepthMode::test_write, scene});
    sky_pipeline_ = device.create_graphics_pipeline({Shader::sky_vertex, Shader::sky_pixel, BlendMode::opaque, DepthMode::test, scene});
    collider_lines_pipeline_ = device.create_graphics_pipeline(
        {Shader::collider_lines_vertex, Shader::debug_lines_pixel, BlendMode::opaque, DepthMode::test, scene, Topology::lines});
    if (!environment_buffer_ || !prop_vertices_ || !props_pipeline_ || !terrain_pipeline_ || !sky_pipeline_ || !collider_lines_pipeline_)
    {
        return false;
    }
    const auto statistics = environment_.statistics();
    SDL_Log("Test range ready: %u x %u height samples, %.0f m across; %u props, %u boxes in %u cells (longest list %u), "
        "%u prop vertices, %u environment bytes.", Terrain::resolution, Terrain::resolution, 2.0 * Terrain::half_extent,
        statistics.props, statistics.boxes, statistics.occupied_cells, statistics.longest_list, prop_vertex_count_, statistics.bytes);
    return true;
}

RangeDraws TestRange::draws() const
{
    assert(environment_buffer_ && prop_vertices_ && props_pipeline_ && terrain_pipeline_ && sky_pipeline_ && collider_lines_pipeline_);
    Bindings props{};
    props.read[0] = prop_vertices_.get();
    props.read[1] = environment_buffer_.get();
    Bindings environment{};
    environment.read[0] = environment_buffer_.get();
    constexpr auto cells = Terrain::resolution - 1;
    const auto boxes = environment_.box_count();
    return {Draw{*props_pipeline_, props, {}, prop_vertex_count_, 1, "Props"},
        Draw{*terrain_pipeline_, environment, {}, cells * cells * 6, 1, "Terrain"},
        Draw{*sky_pipeline_, {}, {}, 3, 1, "Sky"},
        Draw{*collider_lines_pipeline_, environment, {boxes}, 24, boxes, "Collider lines"}};
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
        camera.position = {190, terrain().height(190, 0) + 2, 0};
        camera.look_at({0, 6, 0});
        break;
    case RangeView::aerial:
        camera.position = {0, 140, 120};
        camera.look_at({0, 0, 0});
        break;
    case RangeView::range:
        camera.position = {-44, 10, -8};
        camera.look_at({0, 1, -52});
        break;
    }
    return camera;
}
}
