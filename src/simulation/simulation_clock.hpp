#pragma once

#include <cstdint>

namespace pyroclast
{
inline constexpr std::uint32_t ticks_per_second = 60;
inline constexpr double tick_seconds = 1.0 / ticks_per_second;

class SimulationClock final
{
public:
    std::uint32_t tick() const { return tick_; }
    std::uint32_t upcoming_steps(double elapsed_seconds) const;
    std::uint32_t advance(double elapsed_seconds);
    void commit(std::uint32_t steps);
    void set_fixed_ticks(bool fixed) { fixed_ticks_ = fixed; }
    void prepare_profile(std::uint32_t tick);
    bool has_pending_steps() const { return pending_steps_ != 0; }
    bool stepping() const { return !paused_ || pending_steps_ != 0; }
    void discard_pending();
    bool paused() const { return paused_; }
    void toggle_pause();
    void step();
    void set_time_scale(double scale);
    double time_scale() const { return time_scale_; }
    std::uint64_t capped_frames() const { return capped_frames_; }

private:
    std::uint32_t tick_ = 0;
    std::uint32_t pending_steps_ = 0;
    double accumulator_ = 0;
    double time_scale_ = 1;
    std::uint64_t capped_frames_ = 0;
    bool paused_ = false;
    bool fixed_ticks_ = false;
};
}
