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
class GraphicsDevice;

enum class BlendMode { opaque, additive, premultiplied_alpha };

// none: passes without a depth target only. test: reverse Z, greater or equal. test_write needs a writable depth target.
enum class DepthMode { none, ignore, test, test_write };

// Resources record the device that created them (null in tests), so backends downcast without RTTI.
class Buffer
{
public:
    explicit Buffer(std::uint32_t size, const GraphicsDevice* owner = nullptr) : size_(size), owner_(owner) {}
    virtual ~Buffer() = default;
    std::uint32_t size() const { return size_; }
    const GraphicsDevice* owner() const { return owner_; }

private:
    std::uint32_t size_;
    const GraphicsDevice* owner_;
};

enum class Topology { triangles, lines };

// color is the backbuffer's 8-bit UNORM format; depth is 32-bit float, sampled as its red channel.
enum class TargetFormat { color, rgba16_float, r11g11b10_float, depth };

class GraphicsPipeline
{
public:
    explicit GraphicsPipeline(BlendMode blend, DepthMode depth = DepthMode::none, TargetFormat target = TargetFormat::color,
        const GraphicsDevice* owner = nullptr)
        : blend_(blend), depth_(depth), target_(target), owner_(owner) {}
    virtual ~GraphicsPipeline() = default;
    const GraphicsDevice* owner() const { return owner_; }
    BlendMode blend() const { return blend_; }
    DepthMode depth() const { return depth_; }
    TargetFormat target() const { return target_; }

private:
    BlendMode blend_;
    DepthMode depth_;
    TargetFormat target_;
    const GraphicsDevice* owner_;
};

class ComputePipeline
{
public:
    explicit ComputePipeline(const GraphicsDevice* owner = nullptr) : owner_(owner) {}
    virtual ~ComputePipeline() = default;
    const GraphicsDevice* owner() const { return owner_; }

private:
    const GraphicsDevice* owner_;
};

struct GraphicsPipelineDescription
{
    Shader vertex_shader;
    Shader pixel_shader;
    BlendMode blend = BlendMode::opaque;
    DepthMode depth = DepthMode::none;
    TargetFormat target = TargetFormat::color;
    Topology topology = Topology::triangles;
};

// The extent is the backbuffer's divided by `divisor`, rounded up; reverse Z clears depth to 0, the far end.
struct RenderTargetDescription
{
    std::uint32_t divisor = 1;
    std::array<float, 4> clear_color{};
    TargetFormat format = TargetFormat::color;
    float clear_depth = 0;
};

class RenderTarget
{
public:
    explicit RenderTarget(const RenderTargetDescription& description, const GraphicsDevice* owner = nullptr)
        : description_(description), owner_(owner) {}
    virtual ~RenderTarget() = default;
    const RenderTargetDescription& description() const { return description_; }
    const GraphicsDevice* owner() const { return owner_; }
    std::array<std::uint32_t, 2> extent_for(std::uint32_t width, std::uint32_t height) const
    {
        const auto divisor = description_.divisor;
        return {(width + divisor - 1) / divisor, (height + divisor - 1) / divisor};
    }

private:
    RenderTargetDescription description_;
    const GraphicsDevice* owner_;
};

// Slots of shaders/binding_layout.hlsli: read t0-t7, write u0-u5 (dispatches only); null slots are unbound.
struct Bindings
{
    std::array<const Buffer*, binding_layout::read_buffer_slots> read{};
    std::array<Buffer*, binding_layout::write_buffer_slots> write{};
};

using DrawConstants = std::array<std::uint32_t, binding_layout::draw_constant_words>;

// Labels name work in GPU captures and must be string literals.
struct Dispatch
{
    const ComputePipeline& pipeline;
    Bindings bindings;
    DrawConstants constants{};
    std::array<std::uint32_t, 3> groups{1, 1, 1};
    const char* label = nullptr;
};

// With `indirect`, the counts are four GPU-written words {vertex count, instance count, 0, 0} at indirect_offset.
struct Draw
{
    const GraphicsPipeline& pipeline;
    Bindings bindings;
    DrawConstants constants{};
    std::uint32_t vertex_count = 0;
    std::uint32_t instance_count = 1;
    const char* label = nullptr;
    const Buffer* indirect = nullptr;
    std::uint32_t indirect_offset = 0;
};

// A null target is the backbuffer. A target is cleared unless `load`; inputs (t8, t9) come from earlier passes.
// A writable depth target is cleared and written; a read-only one holds an earlier pass's depth and may be an input.
struct Pass
{
    RenderTarget* target = nullptr;
    std::array<const RenderTarget*, binding_layout::texture_slots> inputs{};
    std::span<const Draw> draws;
    const char* label = nullptr;
    bool load = false;
    RenderTarget* depth = nullptr;
    bool depth_write = false;
};

// Dispatches, then passes (only the last draws to the backbuffer), then present; barriers come from the bindings.
// The readback is copied between the compute and draw intervals; constants are b1, at most frame_constant_bytes.
struct Frame
{
    std::span<const Dispatch> dispatches;
    std::span<const Pass> passes;
    std::array<float, 4> clear_color{};
    std::span<const std::byte> constants;
    bool capture = false;
    ImDrawData* overlay = nullptr;
    struct Readback
    {
        const Buffer* source = nullptr;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
    } readback;
};

inline constexpr std::uint32_t max_readback_bytes = 256;

// Published by the render call that reuses the frame's slot; sample_id grows with each readback.
struct ReadbackData
{
    std::array<std::byte, max_readback_bytes> bytes{};
    std::uint32_t size = 0;
    std::uint64_t sample_id = 0;
};

// RGBA bytes, rows top to bottom; alpha is always 255.
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

// compute_ms: the dispatches; draw_ms: the passes, ending before the overlay; frame_ms: first dispatch to draw end.
struct GpuTimings
{
    double compute_ms = 0;
    double draw_ms = 0;
    std::uint64_t sample_id = 0;
    std::array<GpuRegion, max_gpu_regions> regions{};
    std::uint32_t region_count = 0;
    double frame_ms = 0;
};

// CPU blocking in the last submitted render call: waiting for its frame slot, and presenting.
struct SubmitTimings
{
    double wait_ms = 0;
    double present_ms = 0;
};

// `mode` is a string literal.
struct PresentInfo
{
    std::string_view mode;
    std::uint32_t images = 0;
};

// Bytes as the driver estimates them; the APIs count differently, so they are not comparable across them.
struct GpuMemory
{
    std::uint64_t local_usage = 0;
    std::uint64_t local_budget = 0;
    std::uint64_t non_local_usage = 0;
    std::uint64_t non_local_budget = 0;
};

class GraphicsDevice
{
public:
    virtual ~GraphicsDevice() = default;
    // Returned resources belong to this device. Size: a nonzero multiple of 4; an empty `initial` zero-fills
    // on the GPU, otherwise it holds exactly `size` bytes.
    virtual std::unique_ptr<Buffer> create_buffer(std::uint32_t size, std::span<const std::byte> initial) = 0;
    virtual std::unique_ptr<GraphicsPipeline> create_graphics_pipeline(
        const GraphicsPipelineDescription& description) = 0;
    virtual std::unique_ptr<ComputePipeline> create_compute_pipeline(Shader shader) = 0;
    virtual std::unique_ptr<RenderTarget> create_render_target(const RenderTargetDescription& description) = 0;
    virtual bool resize(int width, int height) = 0;
    virtual RenderResult render(const Frame& frame) = 0;
    virtual bool wait_idle() = 0;
    virtual std::optional<GpuTimings> completed_gpu_timings() const = 0;
    // Each submitted readback arrives once, in order, when its frame slot is reused (a later render call).
    virtual std::optional<ReadbackData> completed_readback() const = 0;
    // Waits for the GPU and returns the image copied by the last submitted frame that requested a capture.
    virtual std::optional<CapturedImage> read_capture() = 0;
    // The selected GPU as the driver reports it; valid for the device's lifetime.
    virtual std::string_view adapter_name() const = 0;
    virtual SubmitTimings last_submit_timings() const = 0;
    virtual PresentInfo present_info() const = 0;
    virtual std::optional<GpuMemory> memory_usage() const = 0;
    virtual std::uint64_t swapchain_rebuilds() const = 0;
};

struct GraphicsDeviceCreation
{
    std::unique_ptr<GraphicsDevice> device;
    std::string error;
};

// gpu_regions implies gpu_timing and serializes Vulkan GPU work. With debug_overlay, the ImGui context must
// exist from device creation until the device is destroyed.
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
