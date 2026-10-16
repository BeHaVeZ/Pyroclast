#pragma once

#include "frame_constants.hpp"
#include "../simulation/explosion_system.hpp"
#include "../simulation/rotorcraft.hpp"
#include "../simulation/wind.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace pyroclast
{
// emission in 1/1024.
struct RotorFrame
{
    bool exists = false;
    bool listed = false;
    HelicopterPose pose;
    std::array<RotorStep, particle_layout::maximum_simulation_steps> steps{};
    std::uint32_t step_count = 0;
    RotorStep drawn;
    DustCensus census;
    std::optional<std::uint32_t> slot;
    float3 key;
    float3 disc_key;
    std::uint32_t emission = 0;
    std::int32_t hub_height_cm = 0;
};

// Overwrites the history at each step tick, over whatever a skipped frame left, so the census counts what the GPU
// is given.
template <typename DriftAt>
RotorFrame prepare_rotor(Rotorcraft& rotorcraft, const ExplosionSystem& effects, const Terrain& terrain, const WindSettings& wind, bool gusts,
    std::uint32_t mask, std::uint32_t tick, std::uint32_t steps, std::uint32_t drawn_tick, const DriftAt& drift_at)
{
    RotorFrame frame;
    const auto& helicopter = rotorcraft.helicopter();
    if (!helicopter) return frame;
    frame.exists = true;
    const auto& plan = helicopter->plan;
    frame.listed = drawn_tick >= plan.list_tick && drawn_tick < plan.unlist_tick;
    frame.step_count = std::min(steps, particle_layout::maximum_simulation_steps);
    for (std::uint32_t step = 0; step < frame.step_count; ++step)
    {
        const std::uint32_t at = tick + step + 1;
        frame.steps[step] = rotor_step(plan, at, terrain, wind, gusts, mask, helicopter->reference);
        rotorcraft.history().store(at, frame.steps[step], drift_at(at));
    }
    frame.drawn = frame.step_count != 0 && drawn_tick == tick + frame.step_count ? frame.steps[frame.step_count - 1]
                                                                                 : rotor_step(plan, drawn_tick, terrain, wind, gusts, mask, helicopter->reference);
    frame.pose = helicopter_pose(plan, drawn_tick, terrain, wind, gusts, mask);
    frame.emission = frame.drawn.emission;
    frame.hub_height_cm = static_cast<std::int32_t>(std::lround((frame.pose.skid_height + rotor::hub_above_skids) * 100));
    frame.slot = effects.rotor_slot();
    const float3 hub = to_float3(frame.pose.hub);
    frame.key = hub;
    frame.disc_key = hub;
    if (frame.slot)
    {
        const auto& slot = effects.slots()[*frame.slot];
        const bool dust_drawn = ((mask >> particle_layout::layer_ground_dust) & 1) != 0;
        frame.census = dust_census(rotorcraft.history(), slot.seed, slot.start_tick, slot.start_tick + slot.lifetime_ticks, drawn_tick,
            drift_at(drawn_tick), dust_drawn);
        if (frame.census.alive != 0)
        {
            frame.key = to_float3(frame.census.mean_point) + float3{0, 4, 0};
            const double above = std::clamp((frame.hub_height_cm / 100.0 - 1.5 * rotor::radius) / (1.5 * rotor::radius), 0.0, 1.0);
            const double low = 1 - above * above * (3 - 2 * above);
            frame.disc_key = low >= 1 ? frame.key : hub + (frame.key - hub) * static_cast<float>(low);
        }
    }
    return frame;
}

void fill_rotor_constants(FrameConstants& constants, const Rotorcraft& rotorcraft, const RotorFrame& frame);
}
