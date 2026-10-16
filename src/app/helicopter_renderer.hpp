#pragma once

#include "rotor_constants.hpp"
#include "../graphics/graphics_device.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace pyroclast
{
// b0 of shaders/helicopter.hlsl and helicopter_shadow.hlsl: pose rows (body to world), then the dither level (0-16),
// or for the shadow its first cell column and row and the fade in 1/65536.
struct HelicopterConstants
{
    std::array<float4, 3> rows{};
    std::array<std::uint32_t, 4> state{};
};
static_assert(sizeof(HelicopterConstants) == sizeof(DrawConstants) && offsetof(HelicopterConstants, state) == 48);

class HelicopterRenderer final
{
public:
    bool initialize(GraphicsDevice& device, TargetFormat scene_format);
    void scene_draws(const RotorFrame& frame, const Buffer& environment, float3 sun, std::vector<Draw>& opaque, std::vector<Draw>& after_sky) const;
    std::uint32_t mesh_vertices() const { return vertex_count_; }

private:
    std::unique_ptr<Buffer> mesh_;
    std::uint32_t vertex_count_ = 0;
    std::unique_ptr<GraphicsPipeline> fuselage_;
    std::unique_ptr<GraphicsPipeline> shadow_;
};
}
