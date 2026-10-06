#pragma once

#include "../graphics/graphics_device.hpp"

namespace pyroclast
{
class TriangleDemo final
{
public:
    bool initialize(GraphicsDevice& device, Renderer renderer);
    bool render(GraphicsDevice& device) const;
    void toggle_colors();

private:
    std::unique_ptr<VertexBuffer> vertices_;
    std::unique_ptr<GraphicsPipeline> pipeline_;
    std::unique_ptr<ComputePipeline> compute_;
    std::uint32_t color_phase_ = 0;
};
}
