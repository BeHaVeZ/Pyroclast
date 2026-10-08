#include "smoke_resolution_policy.hpp"

#include <algorithm>

namespace pyroclast
{
double SmokeResolutionPolicy::limit(const SmokeBudget& budget)
{
    if (budget.limit > 0) return budget.limit;
    return budget.count ? default_smoke_particles : default_coverage_screens;
}

double SmokeResolutionPolicy::load(const ParticleCounters& counters, const SmokeBudget& budget, double screen_pixels)
{
    if (budget.count) return counters.smoke;
    const auto& bins = counters.smoke_coverage;
    const double units = static_cast<double>(bins[0]) + bins[1] + bins[2] + bins[3];
    return units * particle_layout::coverage_unit_pixels / std::max(screen_pixels, 1.0);
}

bool SmokeResolutionPolicy::decide(const std::optional<ParticleCounters>& newest, const SmokeBudget& budget, double screen_pixels) const
{
    if (!newest || since_switch_ < dwell_frames || (!budget.count && !newest->measured)) return half_;
    const double measured = load(*newest, budget, screen_pixels);
    const double up = limit(budget);
    if (!half_) return measured >= up;
    return measured > up * down_share;
}

void SmokeResolutionPolicy::commit(bool half)
{
    if (half != half_)
    {
        half_ = half;
        since_switch_ = 0;
        ++switches_;
    }
    else if (since_switch_ < dwell_frames) ++since_switch_;
    if (half) ++half_frames_;
}

void SmokeResolutionPolicy::follow(bool half)
{
    half_ = half;
    since_switch_ = dwell_frames;
}
}
