#pragma once

#include "graphics_api.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

struct SDL_Window;

namespace pyroclast
{
struct Vertex
{
    float position[3];
    float color[3];
};
static_assert(sizeof(Vertex) == 24 && offsetof(Vertex, color) == 12);

class VertexBuffer
{
public:
    virtual ~VertexBuffer() = default;
    virtual std::uint32_t vertex_count() const = 0;
};

class GraphicsPipeline
{
public:
    virtual ~GraphicsPipeline() = default;
};

struct GraphicsPipelineDescription
{
    std::span<const std::byte> vertex_shader;
    std::span<const std::byte> pixel_shader;
};

struct DrawCommand
{
    const VertexBuffer& vertices;
    const GraphicsPipeline& pipeline;
    std::array<float, 4> clear_color;
};

class GraphicsDevice
{
public:
    virtual ~GraphicsDevice() = default;
    virtual std::unique_ptr<VertexBuffer> create_vertex_buffer(std::span<const Vertex> vertices) = 0;
    virtual std::unique_ptr<GraphicsPipeline> create_graphics_pipeline(
        const GraphicsPipelineDescription& description) = 0;
    virtual bool resize(int width, int height) = 0;
    virtual bool render(const DrawCommand& draw) = 0;
    virtual bool wait_idle() = 0;
};

struct GraphicsDeviceCreation
{
    std::unique_ptr<GraphicsDevice> device;
    std::string error;
};

GraphicsDeviceCreation create_graphics_device(Renderer renderer, SDL_Window* window);
}
