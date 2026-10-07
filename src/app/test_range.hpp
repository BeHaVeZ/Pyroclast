#pragma once

#include "camera.hpp"
#include "frame_constants.hpp"
#include "range_view.hpp"
#include "../graphics/graphics_device.hpp"
#include "../simulation/environment.hpp"

#include <array>
#include <memory>

namespace pyroclast
{
class TestRange final
{
public:
    bool initialize(GraphicsDevice& device, TargetFormat scene);
    const Environment& environment() const { return environment_; }
    const Terrain& terrain() const { return environment_.terrain(); }
    const Buffer& environment_buffer() const { return *environment_buffer_; }
    std::array<Draw, 2> draws() const;
    static constexpr float exposure = 1.25f;
    static void light(FrameConstants& constants);
    Camera view(RangeView name) const;

private:
    Environment environment_;
    std::unique_ptr<Buffer> environment_buffer_;
    std::unique_ptr<GraphicsPipeline> terrain_pipeline_;
    std::unique_ptr<GraphicsPipeline> sky_pipeline_;
};
}
