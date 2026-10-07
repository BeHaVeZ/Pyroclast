#pragma once

#include "camera.hpp"
#include "frame_constants.hpp"
#include "range_view.hpp"
#include "../graphics/graphics_device.hpp"
#include "../simulation/terrain.hpp"

#include <array>
#include <memory>

namespace pyroclast
{
class TestRange final
{
public:
    bool initialize(GraphicsDevice& device, TargetFormat scene);
    const Terrain& terrain() const { return terrain_; }
    const Buffer& heights() const { return *heights_; }
    std::array<Draw, 2> draws() const;
    static constexpr float exposure = 1.25f;
    static void light(FrameConstants& constants);
    Camera view(RangeView name) const;

private:
    Terrain terrain_;
    std::unique_ptr<Buffer> heights_;
    std::unique_ptr<GraphicsPipeline> terrain_pipeline_;
    std::unique_ptr<GraphicsPipeline> sky_pipeline_;
};
}
