#pragma once

#include "camera.hpp"
#include "frame_constants.hpp"
#include "../graphics/graphics_device.hpp"
#include "../simulation/terrain.hpp"

#include <array>
#include <memory>

namespace pyroclast
{
class TestRange final
{
public:
    bool initialize(GraphicsDevice& device);
    const Terrain& terrain() const { return terrain_; }
    const Buffer& heights() const { return *heights_; }
    std::array<Draw, 2> draws() const;
    static void light(FrameConstants& constants);
    static Camera overview();

private:
    Terrain terrain_;
    std::unique_ptr<Buffer> heights_;
    std::unique_ptr<GraphicsPipeline> terrain_pipeline_;
    std::unique_ptr<GraphicsPipeline> sky_pipeline_;
};
}
