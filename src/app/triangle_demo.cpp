#include "triangle_demo.hpp"

#include "../graphics/triangle_shaders.hpp"

namespace pyroclast
{
bool TriangleDemo::initialize(GraphicsDevice& device, Renderer renderer)
{
    constexpr Vertex vertices[] = {
        {{0.0f, 0.6f, 0.0f}, {0.0f, 0.0f, 0.0f}},
        {{0.6f, -0.6f, 0.0f}, {0.0f, 0.0f, 0.0f}},
        {{-0.6f, -0.6f, 0.0f}, {0.0f, 0.0f, 0.0f}}
    };
    pipeline_ = device.create_graphics_pipeline(triangle_shaders(renderer));
    if (!pipeline_)
    {
        return false;
    }
    vertices_ = device.create_vertex_buffer(vertices);
    if (!vertices_)
    {
        return false;
    }
    compute_ = device.create_compute_pipeline(triangle_compute_shader(renderer), *vertices_);
    return compute_ != nullptr;
}

bool TriangleDemo::render(GraphicsDevice& device) const
{
    const ComputeDispatch compute{*compute_, {vertices_->vertex_count(), color_phase_}, 1};
    const DrawRange range{*pipeline_, 0, vertices_->vertex_count()};
    return device.render({*vertices_, {&range, 1}, {0.04f, 0.07f, 0.12f, 1.0f}, &compute}) != RenderResult::failed;
}

void TriangleDemo::toggle_colors()
{
    color_phase_ ^= 1u;
}
}
