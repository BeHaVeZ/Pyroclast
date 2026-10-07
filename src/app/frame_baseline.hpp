#pragma once

#include "../graphics/graphics_device.hpp"
#include <array>
#include <cstddef>
#include <string>

namespace pyroclast
{
#ifdef NDEBUG
inline constexpr const char* build_configuration = "Release";
#else
inline constexpr const char* build_configuration = "Debug";
#endif

struct SampleDescription
{
    std::string renderer;
    std::string gpu;
    std::uint32_t explosions = 1;
    std::uint32_t capacity = 0;
    std::optional<std::uint32_t> profile_tick;
    bool half_resolution_smoke = false;
    std::string view = "overview";
    std::string scene_format = "rgba16f";
    std::string preset = "small-he";
    std::uint32_t layer_mask = particle_layout::all_layers_mask;
};

class FrameBaseline final
{
public:
    explicit FrameBaseline(SampleDescription description) : description_(std::move(description)) {}
    void disable() { enabled_ = false; }
    bool enabled() const { return enabled_; }
    void record(double milliseconds, std::optional<GpuTimings> gpu, std::optional<std::uint32_t> alive);

private:
    static constexpr std::size_t warmup_frames = 120;
    SampleDescription description_;
    std::array<double, 600> samples_{};
    std::size_t warmup_ = warmup_frames;
    std::size_t count_ = 0;
    bool enabled_ = true;
    std::uint64_t last_gpu_sample_ = 0;
    std::size_t gpu_count_ = 0;
    double compute_ms_ = 0.0;
    double draw_ms_ = 0.0;
    std::array<double, max_gpu_regions> region_ms_{};
    std::array<const char*, max_gpu_regions> region_labels_{};
    std::uint32_t region_count_ = 0;
};
}
