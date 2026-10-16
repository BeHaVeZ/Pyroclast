#pragma once

#include "jet_constants.hpp"
#include "rotor_constants.hpp"
#include "scalability.hpp"
#include "../simulation/explosion_system.hpp"
#include "../simulation/simulation_clock.hpp"
#include "../simulation/terrain.hpp"
#include "../simulation/wind.hpp"

#include <cstdint>

namespace pyroclast
{
// The simulation moves on only through commit_frame_step.
struct FrameStep
{
    std::uint32_t tick = 0;
    std::uint32_t steps = 0;
    RotorFrame rotor;
    JetFrame jet;

    std::uint32_t drawn_tick() const { return tick + steps; }
};

struct StepSettings
{
    std::uint32_t layer_mask = particle_layout::full_mask;
    WindSettings wind;
    LingerSettings linger;
    SlotPolicy slot_policy = SlotPolicy::drop_new;
};

FrameStep begin_frame_step(SimulationClock& clock, ExplosionSystem& effects, const WindDrift& drift, Rotorcraft* rotorcraft,
    Jetcraft* jetcraft, const Terrain& terrain, const StepSettings& settings, double elapsed_seconds);

// Only for a frame the GPU received.
void commit_frame_step(const FrameStep& step, SimulationClock& clock, WindDrift& drift, const StepSettings& settings);
}
