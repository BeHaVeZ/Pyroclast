#pragma once

#include "graphics_api.hpp"
#include "particle_layout.hpp"
#include "shaders.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Window;

namespace pyroclast
{
enum class BlendMode { opaque, additive, premultiplied_alpha };

class VertexBuffer
{
public:
    VertexBuffer(std::uint32_t record_count, std::uint32_t stride) : record_count_(record_count), stride_(stride) {}
    virtual ~VertexBuffer() = default;
    std::uint32_t record_count() const { return record_count_; }
    std::uint32_t stride() const { return stride_; }

private:
    std::uint32_t record_count_;
    std::uint32_t stride_;
};

class GraphicsPipeline
{
public:
    explicit GraphicsPipeline(BlendMode blend) : blend_(blend) {}
    virtual ~GraphicsPipeline() = default;
    BlendMode blend() const { return blend_; }

private:
    BlendMode blend_;
};

class ComputePipeline
{
public:
    explicit ComputePipeline(const VertexBuffer& buffer) : buffer_(&buffer) {}
    virtual ~ComputePipeline() = default;
    const VertexBuffer& buffer() const { return *buffer_; }

private:
    const VertexBuffer* buffer_;
};

struct ComputeDispatch
{
    const ComputePipeline& pipeline;
    std::array<std::uint32_t, 4> constants;
    std::uint32_t group_count;
};

struct GraphicsPipelineDescription
{
    Shader vertex_shader;
    Shader pixel_shader;
    BlendMode blend = BlendMode::opaque;
};

struct DrawRange
{
    const GraphicsPipeline& pipeline;
    std::uint32_t first_record;
    std::uint32_t record_count;
    std::uint32_t layer;
};

struct RenderTargetDescription
{
    std::uint32_t divisor = 1;
    std::array<float, 4> clear_color{};
};

class RenderTarget
{
public:
    explicit RenderTarget(const RenderTargetDescription& description) : description_(description) {}
    virtual ~RenderTarget() = default;
    const RenderTargetDescription& description() const { return description_; }

private:
    RenderTargetDescription description_;
};

struct Pass
{
    RenderTarget* target = nullptr;
    const RenderTarget* background = nullptr;
    std::span<const DrawRange> draws;
};

struct Frame
{
    const VertexBuffer& vertices;
    std::span<const Pass> passes;
    std::array<float, 4> clear_color;
    const ComputeDispatch* compute = nullptr;
    float aspect_ratio = 1.0f;
    bool capture = false;
};

struct CapturedImage
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
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
        std::span<const std::byte> records, std::uint32_t stride) = 0;
    virtual std::unique_ptr<GraphicsPipeline> create_graphics_pipeline(
        const GraphicsPipelineDescription& description) = 0;
    virtual std::unique_ptr<ComputePipeline> create_compute_pipeline(
        Shader shader, const VertexBuffer& vertices) = 0;
    virtual std::unique_ptr<RenderTarget> create_render_target(const RenderTargetDescription& description) = 0;
    virtual bool resize(int width, int height) = 0;
    virtual RenderResult render(const Frame& frame) = 0;
    virtual bool wait_idle() = 0;
    virtual std::optional<GpuTimings> completed_gpu_timings() const = 0;
    // Waits for the GPU and returns the image copied by the last submitted frame that requested a capture.
    virtual std::optional<CapturedImage> read_capture() = 0;
    // The selected GPU as the driver reports it; valid for the device's lifetime.
    virtual std::string_view adapter_name() const = 0;
};

struct GraphicsDeviceCreation
{
    std::unique_ptr<GraphicsDevice> device;
    std::string error;
};

struct GraphicsDeviceOptions
{
    bool gpu_timing = false;
    bool vsync = true;
    bool capture = false;
};

GraphicsDeviceCreation create_graphics_device(Renderer renderer, SDL_Window* window, const GraphicsDeviceOptions& options = {});
}
