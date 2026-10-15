#pragma once

#include "jet_constants.hpp"
#include "../graphics/graphics_device.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace pyroclast
{
struct JetConstants
{
    std::array<float4, 3> rows{};
    std::array<std::uint32_t, 4> state{};
};
static_assert(sizeof(JetConstants) == sizeof(DrawConstants) && offsetof(JetConstants, state) == 48);

class JetRenderer final
{
public:
    bool initialize(GraphicsDevice& device, TargetFormat scene_format);
    void scene_draws(const JetFrame& frame, const Buffer& environment, float3 sun, float humidity, std::vector<Draw>& opaque,
        std::vector<Draw>& after_sky) const;
    std::uint32_t mesh_vertices() const { return vertex_count_; }

private:
    std::unique_ptr<Buffer> mesh_;
    std::uint32_t vertex_count_ = 0;
    std::unique_ptr<GraphicsPipeline> body_;
    std::unique_ptr<GraphicsPipeline> flame_;
    std::unique_ptr<GraphicsPipeline> vapor_;
    std::unique_ptr<GraphicsPipeline> trails_;
    std::unique_ptr<GraphicsPipeline> shadow_;
};
}
