#pragma once

#include "../graphics/graphics_device.hpp"
#include <array>
#include <cstddef>

namespace pyroclast
{
#ifdef NDEBUG
inline constexpr const char* build_configuration = "Release";
#else
inline constexpr const char* build_configuration = "Debug";
#endif

class FrameBaseline final
{
public:
    FrameBaseline(const char* renderer, std::uint32_t explosions, std::uint32_t capacity, std::optional<std::uint32_t> profile_tick)
        : renderer_(renderer), explosions_(explosions), capacity_(capacity), profile_tick_(profile_tick) {}
    void disable() { enabled_ = false; }
    void record(double milliseconds, std::optional<GpuTimings> gpu);

private:
    const char* renderer_;
    std::uint32_t explosions_;
    std::uint32_t capacity_;
    std::optional<std::uint32_t> profile_tick_;
    std::array<double, 600> samples_{};
    std::size_t warmup_ = 120;
    std::size_t count_ = 0;
    bool enabled_ = true;
    std::uint64_t last_gpu_sample_ = 0;
    std::size_t gpu_count_ = 0;
    double compute_ms_ = 0.0;
    double draw_ms_ = 0.0;
};
}
