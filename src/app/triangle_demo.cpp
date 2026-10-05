#include "triangle_demo.hpp"

#include "triangle_vs.hpp"
#include "triangle_ps.hpp"

namespace pyroclast
{
bool TriangleDemo::initialize(GraphicsDevice& device)
{
    constexpr Vertex vertices[] = {
        {{0.0f, 0.6f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{0.6f, -0.6f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{-0.6f, -0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}}
    };
    pipeline_ = device.create_graphics_pipeline({
        std::as_bytes(std::span(triangle_vs)), std::as_bytes(std::span(triangle_ps))});
    if (!pipeline_)
    {
        return false;
    }
    vertices_ = device.create_vertex_buffer(vertices);
    return vertices_ != nullptr;
}

bool TriangleDemo::render(GraphicsDevice& device) const
{
    return device.render({*vertices_, *pipeline_, {0.04f, 0.07f, 0.12f, 1.0f}});
}
}
