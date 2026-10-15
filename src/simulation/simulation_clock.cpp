#include "simulation_clock.hpp"

#include "../graphics/particle_layout.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cassert>
#include <cmath>

namespace pyroclast
{
namespace
{
constexpr double catch_up_cap = particle_layout::maximum_simulation_steps * tick_seconds;
constexpr std::uint32_t maximum_queued_steps = 600;
}

std::uint32_t SimulationClock::upcoming_steps(double elapsed_seconds) const
{
    if (paused_) return std::min(pending_steps_, 1u);
    if (fixed_ticks_) return 1;
    return static_cast<std::uint32_t>(std::floor(std::min(accumulator_ + elapsed_seconds * time_scale_, catch_up_cap) / tick_seconds));
}

std::uint32_t SimulationClock::advance(double elapsed_seconds)
{
    assert(std::isfinite(elapsed_seconds) && elapsed_seconds >= 0);
    const auto steps = upcoming_steps(elapsed_seconds);
    if (!paused_ && !fixed_ticks_)
    {
        const double wanted = accumulator_ + elapsed_seconds * time_scale_;
        if (wanted > catch_up_cap) ++capped_frames_;
        accumulator_ = std::min(wanted, catch_up_cap);
    }
    return steps;
}

void SimulationClock::commit(std::uint32_t steps)
{
    tick_ += steps;
    if (paused_) pending_steps_ -= std::min(pending_steps_, steps);
    else if (!fixed_ticks_) accumulator_ = std::max(0.0, accumulator_ - steps * tick_seconds);
}

void SimulationClock::prepare_profile(std::uint32_t tick)
{
    assert(tick_ == 0);
    paused_ = true;
    accumulator_ = 0;
    pending_steps_ = tick;
}

void SimulationClock::discard_pending()
{
    pending_steps_ = 0;
    accumulator_ = 0;
}

void SimulationClock::set_time_scale(double scale)
{
    assert(scale == 1.0 || scale == 0.25);
    time_scale_ = scale;
    SDL_Log("Simulation speed: %gx", scale);
}

void SimulationClock::toggle_pause()
{
    paused_ = !paused_;
    pending_steps_ = 0;
    accumulator_ = 0;
    SDL_Log("Particle simulation: %s", paused_ ? "paused" : "running");
}

void SimulationClock::step()
{
    if (paused_ && pending_steps_ < maximum_queued_steps) ++pending_steps_;
}
}
