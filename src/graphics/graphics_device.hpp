#pragma once

#include "binding_layout.hpp"
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
struct ImDrawData;

namespace pyroclast
{
enum class BlendMode { opaque, additive, premultiplied_alpha };

class Buffer
{
public:
    explicit Buffer(std::uint32_t size) : size_(size) {}
    virtual ~Buffer() = default;
    std::uint32_t size() const { return size_; }

private:
    std::uint32_t size_;
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
    virtual ~ComputePipeline() = default;
};

struct GraphicsPipelineDescription
{
    Shader vertex_shader;
    Shader pixel_shader;
    BlendMode blend = BlendMode::opaque;
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

struct Bindings
{
    std::array<const Buffer*, binding_layout::read_buffer_slots> read{};
    std::array<Buffer*, binding_layout::write_buffer_slots> write{};
};

using DrawConstants = std::array<std::uint32_t, binding_layout::draw_constant_words>;

struct Dispatch
{
    const ComputePipeline& pipeline;
    Bindings bindings;
    DrawConstants constants{};
    std::array<std::uint32_t, 3> groups{1, 1, 1};
    const char* label = nullptr;
};

struct Draw
{
    const GraphicsPipeline& pipeline;
    Bindings bindings;
    DrawConstants constants{};
    std::uint32_t vertex_count = 0;
    std::uint32_t instance_count = 1;
    const char* label = nullptr;
};

struct Pass
{
    RenderTarget* target = nullptr;
    std::array<const RenderTarget*, binding_layout::texture_slots> inputs{};
    std::span<const Draw> draws;
    const char* label = nullptr;
};

struct Frame
{
    std::span<const Dispatch> dispatches;
    std::span<const Pass> passes;
    std::array<float, 4> clear_color{};
    std::span<const std::byte> constants;
    bool capture = false;
    ImDrawData* overlay = nullptr;
};

struct CapturedImage
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
};

enum class RenderResult { failed, skipped, submitted };

inline constexpr std::size_t max_gpu_regions = 16;

struct GpuRegion
{
    const char* label = nullptr;
    double milliseconds = 0;
};

struct GpuTimings
{
    double compute_ms = 0;
    double draw_ms = 0;
    std::uint64_t sample_id = 0;
    std::array<GpuRegion, max_gpu_regions> regions{};
    std::uint32_t region_count = 0;
};

class GraphicsDevice
{
public:
    virtual ~GraphicsDevice() = default;
    virtual std::unique_ptr<Buffer> create_buffer(std::uint32_t size, std::span<const std::byte> initial) = 0;
    virtual std::unique_ptr<GraphicsPipeline> create_graphics_pipeline(
        const GraphicsPipelineDescription& description) = 0;
    virtual std::unique_ptr<ComputePipeline> create_compute_pipeline(Shader shader) = 0;
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
    bool gpu_validation = false;
    bool gpu_regions = false;
    std::string adapter;
    bool debug_overlay = false;
};

// The SDL window must outlive the device; resources used by a frame must survive until wait_idle.
GraphicsDeviceCreation create_graphics_device(Renderer renderer, SDL_Window* window, const GraphicsDeviceOptions& options = {});
}
