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
};

class FrameBaseline final
{
public:
    explicit FrameBaseline(SampleDescription description) : description_(std::move(description)) {}
    void disable() { enabled_ = false; }
    void record(double milliseconds, std::optional<GpuTimings> gpu);

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
};
}
