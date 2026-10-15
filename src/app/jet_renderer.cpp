#include "jet_renderer.hpp"
#include "jet_mesh.hpp"

#include <bit>
#include <cmath>
#include <span>

namespace pyroclast
{
bool JetRenderer::initialize(GraphicsDevice& device, TargetFormat scene_format)
{
    const auto vertices = build_jet_mesh();
    vertex_count_ = static_cast<std::uint32_t>(vertices.size());
    mesh_ = device.create_buffer(static_cast<std::uint32_t>(vertices.size() * sizeof(PropVertex)), std::as_bytes(std::span(vertices)));
    body_ = device.create_graphics_pipeline({Shader::jet_vertex, Shader::jet_pixel, BlendMode::opaque, DepthMode::test_write, scene_format});
    return mesh_ && body_;
}

void JetRenderer::scene_draws(const JetFrame& frame, std::vector<Draw>& opaque) const
{
    if (!frame.exists || !frame.pose.listed || frame.pose.fade <= 0) return;
    const auto& pose = frame.pose;
    const float3 forward = to_float3(pose.forward);
    const float3 up = to_float3(pose.up);
    const float3 right = to_float3(pose.right);
    const float3 centre = to_float3(pose.centre);
    JetConstants constants;
    constants.rows = {float4{forward.x, up.x, right.x, centre.x}, float4{forward.y, up.y, right.y, centre.y}, float4{forward.z, up.z, right.z, centre.z}};
    constexpr float glow = 1.5f;
    constants.state = {static_cast<std::uint32_t>(std::lround(16 * pose.fade)), std::bit_cast<std::uint32_t>(glow), 0, 0};
    if (constants.state[0] == 0) return;
    Bindings mesh{};
    mesh.read[0] = mesh_.get();
    opaque.push_back({*body_, mesh, std::bit_cast<DrawConstants>(constants), vertex_count_, 1, "Jet"});
}
}
