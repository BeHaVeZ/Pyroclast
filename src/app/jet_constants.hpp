#pragma once

#include "frame_constants.hpp"
#include "../simulation/explosion_system.hpp"
#include "../simulation/jet.hpp"
#include "../simulation/terrain.hpp"
#include "../simulation/wind.hpp"

#include <array>
#include <cstdint>

namespace pyroclast
{
struct JetFrame
{
    bool exists = false;
    JetPose pose;
    JetWake wake;
    std::array<JetHead, particle_layout::maximum_simulation_steps> heads{};
    std::array<JetEmission, particle_layout::maximum_simulation_steps> emissions{};
    std::uint32_t step_count = 0;
    std::uint32_t drawn_head = 0;
    std::optional<std::uint32_t> slot;
    JetCensus census;
    float3 key;
};

JetFrame prepare_jet(Jetcraft& jetcraft, const ExplosionSystem& effects, const Terrain& terrain, const WindSettings& wind, bool gusts,
    std::uint32_t mask, std::uint32_t tick, std::uint32_t steps, std::uint32_t drawn_tick);

void fill_jet_constants(FrameConstants& constants, const JetFrame& frame);
}
