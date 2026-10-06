#pragma once

#include "graphics_api.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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

enum class VertexLayout { position_color, particle };
constexpr std::uint32_t vertex_stride(VertexLayout layout)
{
    return layout == VertexLayout::particle ? 32u : sizeof(Vertex);
}

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

class ComputePipeline
{
public:
    virtual ~ComputePipeline() = default;
};

struct ComputeDispatch
{
    const ComputePipeline& pipeline;
    std::array<std::uint32_t, 4> constants;
    std::uint32_t group_count;
};

enum class BlendMode { opaque, additive, premultiplied_alpha };

struct GraphicsPipelineDescription
{
    std::span<const std::byte> vertex_shader;
    std::span<const std::byte> pixel_shader;
    VertexLayout layout = VertexLayout::position_color;
    BlendMode blend = BlendMode::opaque;
};

struct DrawRange
{
    const GraphicsPipeline& pipeline;
    std::uint32_t first_record;
    std::uint32_t record_count;
};

struct DrawCommand
{
    const VertexBuffer& vertices;
    std::span<const DrawRange> ranges;
    std::array<float, 4> clear_color;
    const ComputeDispatch* compute = nullptr;
    float aspect_ratio = 1.0f;
};

enum class RenderResult { failed, skipped, submitted };

struct GpuTimings
{
    double compute_ms;
    double draw_ms;
    std::uint64_t sample_id;
};

class GraphicsDevice
{
public:
    virtual ~GraphicsDevice() = default;
    virtual std::unique_ptr<VertexBuffer> create_vertex_buffer(
        std::span<const std::byte> records, VertexLayout layout) = 0;
    std::unique_ptr<VertexBuffer> create_vertex_buffer(std::span<const Vertex> vertices)
    {
        return create_vertex_buffer(std::as_bytes(vertices), VertexLayout::position_color);
    }
    virtual std::unique_ptr<GraphicsPipeline> create_graphics_pipeline(
        const GraphicsPipelineDescription& description) = 0;
    virtual std::unique_ptr<ComputePipeline> create_compute_pipeline(
        std::span<const std::byte> shader, const VertexBuffer& vertices) = 0;
    virtual bool resize(int width, int height) = 0;
    virtual RenderResult render(const DrawCommand& draw) = 0;
    virtual bool wait_idle() = 0;
    virtual std::optional<GpuTimings> completed_gpu_timings() const = 0;
};

struct GraphicsDeviceCreation
{
    std::unique_ptr<GraphicsDevice> device;
    std::string error;
};

GraphicsDeviceCreation create_graphics_device(Renderer renderer, SDL_Window* window, bool gpu_timing = false);
}
