#include "helicopter_renderer.hpp"
#include "helicopter_mesh.hpp"
#include "../simulation/terrain.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <span>

namespace pyroclast
{
namespace
{
// Kept in step with shadow_cells in shaders/helicopter_shadow.hlsl.
constexpr int shadow_cells = 22;

std::array<float4, 3> pose_rows(const HelicopterPose& pose)
{
    const float3 forward = to_float3(pose.forward);
    const float3 up = to_float3(pose.up);
    const float3 right = to_float3(pose.right);
    const float3 hub = to_float3(pose.hub);
    return {float4{forward.x, up.x, right.x, hub.x}, float4{forward.y, up.y, right.y, hub.y}, float4{forward.z, up.z, right.z, hub.z}};
}
}

bool HelicopterRenderer::initialize(GraphicsDevice& device, TargetFormat scene_format)
{
    const auto vertices = build_helicopter_mesh();
    vertex_count_ = static_cast<std::uint32_t>(vertices.size());
    mesh_ = device.create_buffer(static_cast<std::uint32_t>(vertices.size() * sizeof(PropVertex)), std::as_bytes(std::span(vertices)));
    fuselage_ = device.create_graphics_pipeline({Shader::helicopter_vertex, Shader::helicopter_pixel, BlendMode::opaque, DepthMode::test_write, scene_format});
    shadow_ = device.create_graphics_pipeline(
        {Shader::helicopter_shadow_vertex, Shader::helicopter_shadow_pixel, BlendMode::premultiplied_alpha, DepthMode::test, scene_format});
    return mesh_ && fuselage_ && shadow_;
}

void HelicopterRenderer::scene_draws(const RotorFrame& frame, const Buffer& environment, float3 sun, std::vector<Draw>& opaque,
    std::vector<Draw>& after_sky) const
{
    if (!frame.exists || !frame.listed || frame.pose.fade <= 0) return;
    const auto& pose = frame.pose;
    HelicopterConstants fuselage;
    fuselage.rows = pose_rows(pose);
    fuselage.state = {static_cast<std::uint32_t>(std::lround(16 * pose.fade)), 0, 0, 0};
    Bindings mesh{};
    mesh.read[0] = mesh_.get();
    mesh.read[1] = &environment;
    if (fuselage.state[0] != 0) opaque.push_back({*fuselage_, mesh, std::bit_cast<DrawConstants>(fuselage), vertex_count_, 1, "Helicopter"});
    HelicopterConstants shadow;
    shadow.rows = fuselage.rows;
    const float3 hub = to_float3(pose.hub);
    const float drop = static_cast<float>(pose.skid_height + rotor::hub_above_skids);
    const float reach = drop / std::max(sun.y, 0.1f);
    const float foot_x = hub.x - sun.x * reach;
    const float foot_z = hub.z - sun.z * reach;
    const auto first_cell = [](float coordinate) {
        return static_cast<int>(std::floor((coordinate + Terrain::half_extent) / Terrain::spacing)) - shadow_cells / 2;
    };
    shadow.state = {static_cast<std::uint32_t>(first_cell(foot_x)), static_cast<std::uint32_t>(first_cell(foot_z)),
        static_cast<std::uint32_t>(std::lround(65536 * pose.fade)), 0};
    Bindings ground{};
    ground.read[0] = &environment;
    after_sky.push_back({*shadow_, ground, std::bit_cast<DrawConstants>(shadow), shadow_cells * shadow_cells * 6, 1, "Helicopter shadow"});
}
}
