#include "jet_renderer.hpp"
#include "jet_mesh.hpp"
#include "../simulation/terrain.hpp"

#include <algorithm>
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
    flame_ = device.create_graphics_pipeline({Shader::jet_flame_vertex, Shader::jet_flame_pixel, BlendMode::additive, DepthMode::test, scene_format});
    vapor_ = device.create_graphics_pipeline(
        {Shader::jet_vapor_vertex, Shader::jet_vapor_pixel, BlendMode::premultiplied_alpha, DepthMode::test, scene_format});
    trails_ = device.create_graphics_pipeline(
        {Shader::jet_trails_vertex, Shader::jet_trails_pixel, BlendMode::premultiplied_alpha, DepthMode::test, scene_format});
    shadow_ = device.create_graphics_pipeline(
        {Shader::jet_shadow_vertex, Shader::jet_shadow_pixel, BlendMode::premultiplied_alpha, DepthMode::test, scene_format});
    return mesh_ && body_ && flame_ && vapor_ && trails_ && shadow_;
}

namespace
{
constexpr int shadow_cells = 22;
constexpr std::uint32_t flame_vertices = (12 - 1) * 12 * 6;
constexpr std::uint32_t vapor_vertices = (12 - 1) * 28 * 6;
constexpr std::uint32_t trail_vertices = 2 * 33 * 6;
}

void JetRenderer::scene_draws(const JetFrame& frame, const Buffer& environment, float3 sun, float humidity, std::vector<Draw>& opaque,
    std::vector<Draw>& after_sky) const
{
    if (!frame.exists) return;
    const Draw trails{*trails_, {}, {}, trail_vertices, 1, "Jet condensation trails"};
    const auto& pose = frame.pose;
    if (!pose.listed || pose.fade <= 0 || std::lround(16 * pose.fade) == 0)
    {
        if (frame.wake.count != 0) after_sky.push_back(trails);
        return;
    }
    const float3 forward = to_float3(pose.forward);
    const float3 up = to_float3(pose.up);
    const float3 right = to_float3(pose.right);
    const float3 centre = to_float3(pose.centre);
    JetConstants constants;
    constants.rows = {float4{forward.x, up.x, right.x, centre.x}, float4{forward.y, up.y, right.y, centre.y}, float4{forward.z, up.z, right.z, centre.z}};
    const auto glow = static_cast<float>(1.5 + 6 * pose.afterburner);
    const auto burner = static_cast<float>(pose.afterburner);
    const auto vapor = static_cast<float>(vapor_strength(pose, humidity));
    constants.state = {static_cast<std::uint32_t>(std::lround(16 * pose.fade)), std::bit_cast<std::uint32_t>(glow), std::bit_cast<std::uint32_t>(burner),
        std::bit_cast<std::uint32_t>(vapor)};
    Bindings mesh{};
    mesh.read[0] = mesh_.get();
    opaque.push_back({*body_, mesh, std::bit_cast<DrawConstants>(constants), vertex_count_, 1, "Jet"});
    JetConstants shadow;
    shadow.rows = constants.rows;
    const float ground = frame.wake.count != 0 ? frame.wake.knots[0].words[2].x : 0.0f;
    const float drop = std::max(centre.y - ground, 0.0f);
    const float reach = drop / std::max(sun.y, 0.1f);
    const auto first_cell = [](float coordinate) {
        return static_cast<int>(std::floor((coordinate + Terrain::half_extent) / Terrain::spacing)) - shadow_cells / 2;
    };
    shadow.state = {static_cast<std::uint32_t>(first_cell(centre.x - sun.x * reach)), static_cast<std::uint32_t>(first_cell(centre.z - sun.z * reach)),
        static_cast<std::uint32_t>(std::lround(65536 * pose.fade)), 0};
    Bindings ground_bindings{};
    ground_bindings.read[0] = &environment;
    after_sky.push_back({*shadow_, ground_bindings, std::bit_cast<DrawConstants>(shadow), shadow_cells * shadow_cells * 6, 1, "Jet shadow"});
    if (frame.wake.count != 0) after_sky.push_back(trails);
    if (burner > 0) after_sky.push_back({*flame_, {}, std::bit_cast<DrawConstants>(constants), flame_vertices, 1, "Jet afterburner"});
    if (vapor > 0) after_sky.push_back({*vapor_, {}, std::bit_cast<DrawConstants>(constants), vapor_vertices, 1, "Jet vapor cone"});
}
}
