#pragma once

#include "../graphics/graphics_device.hpp"

namespace pyroclast
{
class TriangleDemo final
{
public:
    bool initialize(GraphicsDevice& device);
    bool render(GraphicsDevice& device) const;

private:
    std::unique_ptr<VertexBuffer> vertices_;
    std::unique_ptr<GraphicsPipeline> pipeline_;
};
}
