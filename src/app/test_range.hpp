#pragma once

#include "camera.hpp"
#include "frame_constants.hpp"
#include "range_view.hpp"
#include "../graphics/graphics_device.hpp"
#include "../simulation/environment.hpp"

#include <cstdint>
#include <memory>

namespace pyroclast
{
struct RangeDraws
{
    Draw props;
    Draw terrain;
    Draw sky;
    Draw colliders;
};

class TestRange final
{
public:
    bool initialize(GraphicsDevice& device, TargetFormat scene);
    const Environment& environment() const { return environment_; }
    const Terrain& terrain() const { return environment_.terrain(); }
    const Buffer& environment_buffer() const { return *environment_buffer_; }
    RangeDraws draws() const;
    std::uint32_t prop_vertex_count() const { return prop_vertex_count_; }
    static constexpr float exposure = 1.25f;
    static void light(FrameConstants& constants);
    Camera view(RangeView name) const;

private:
    Environment environment_;
    std::unique_ptr<Buffer> environment_buffer_;
    std::unique_ptr<Buffer> prop_vertices_;
    std::uint32_t prop_vertex_count_ = 0;
    std::unique_ptr<GraphicsPipeline> props_pipeline_;
    std::unique_ptr<GraphicsPipeline> terrain_pipeline_;
    std::unique_ptr<GraphicsPipeline> sky_pipeline_;
    std::unique_ptr<GraphicsPipeline> collider_lines_pipeline_;
};
}
