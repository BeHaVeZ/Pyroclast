#pragma once

#include "scalability.hpp"
#include "../simulation/particle_system.hpp"

#include <cstdint>
#include <optional>

namespace pyroclast
{
class SmokeResolutionPolicy final
{
public:
    static constexpr double default_coverage_screens = 1000;
    static constexpr double default_smoke_particles = 60000;
    static constexpr double down_share = 0.8;
    static constexpr std::uint32_t dwell_frames = 30;

    static double limit(const SmokeBudget& budget);
    static double load(const ParticleCounters& counters, const SmokeBudget& budget, double screen_pixels);

    bool decide(const std::optional<ParticleCounters>& newest, const SmokeBudget& budget, double screen_pixels) const;
    void commit(bool half);
    bool half() const { return half_; }
    std::uint64_t half_frames() const { return half_frames_; }
    std::uint64_t switches() const { return switches_; }

private:
    bool half_ = false;
    std::uint32_t since_switch_ = dwell_frames;
    std::uint64_t half_frames_ = 0;
    std::uint64_t switches_ = 0;
};
}
