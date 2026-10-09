#include "rotor_constants.hpp"

#include <algorithm>
#include <bit>

namespace pyroclast
{
namespace
{
float4 hub_of(const RotorStep& step)
{
    return {static_cast<float>(step.hub.x), static_cast<float>(step.hub.y), static_cast<float>(step.hub.z), static_cast<float>(step.inflow)};
}

float4 wake_of(const RotorStep& step)
{
    return {static_cast<float>(step.skew_x), static_cast<float>(step.skew_z), static_cast<float>(step.height), static_cast<float>(step.ring)};
}

float4 jet_of(const RotorStep& step)
{
    return {static_cast<float>(step.jet_flow), static_cast<float>(step.jet_radius), static_cast<float>(step.ground.x), static_cast<float>(step.ground.z)};
}
}

void fill_rotor_constants(FrameConstants& constants, const Rotorcraft& rotorcraft, const RotorFrame& frame)
{
    constants.rotor_state = {};
    constants.rotor_hubs = {};
    constants.rotor_wakes = {};
    constants.rotor_jets = {};
    constants.rotor_births = {};
    constants.rotor_draw = {};
    constants.rotor_discs = {};
    constants.rotor_dust_shadow = {};
    const auto& helicopter = rotorcraft.helicopter();
    if (!helicopter || !frame.exists) return;
    const auto& plan = helicopter->plan;
    const auto& pose = frame.pose;
    constants.rotor_state = {1, plan.list_tick, plan.unlist_tick, std::bit_cast<std::uint32_t>(static_cast<float>(pose.fade))};
    for (std::uint32_t step = 0; step < frame.step_count; ++step)
    {
        const auto& state = frame.steps[step];
        constants.rotor_hubs[step] = hub_of(state);
        constants.rotor_wakes[step] = wake_of(state);
        constants.rotor_jets[step] = jet_of(state);
        constants.rotor_births[step] = {state.emission, state.radii_cm[0] | (state.radii_cm[1] << 16), state.radii_cm[2] | (state.radii_cm[3] << 16), 0};
    }
    constants.rotor_draw = {hub_of(frame.drawn), wake_of(frame.drawn), jet_of(frame.drawn)};
    const float3 forward = to_float3(pose.forward);
    const float3 up = to_float3(pose.up);
    const float3 right = to_float3(pose.right);
    const float3 hub = to_float3(pose.hub);
    const auto exposure = static_cast<float>(0.05 * pose.spin);
    const float3 tail = hub - forward * static_cast<float>(rotor::tail_hub_aft) - up * static_cast<float>(rotor::tail_hub_below) +
        right * static_cast<float>(rotor::tail_disc_side);
    constants.rotor_discs = {float4{hub.x, hub.y, hub.z, rotor_layout::rotor_radius},
        float4{forward.x, forward.y, forward.z, static_cast<float>(pose.main_phase)},
        float4{right.x, right.y, right.z, rotor_layout::rotor_speed * exposure},
        float4{tail.x, tail.y, tail.z, rotor_layout::tail_rotor_radius},
        float4{forward.x, forward.y, forward.z, static_cast<float>(pose.tail_phase)},
        float4{up.x, up.y, up.z, rotor_layout::tail_rotor_speed * exposure}};
    const auto& census = frame.census;
    if (census.alive != 0)
    {
        const auto radius = static_cast<float>(census.mean_outer + census.mean_age);
        const float3 centre = to_float3(census.mean_point);
        constants.rotor_dust_shadow = {float4{centre.x, centre.y + 0.5f * radius, centre.z, radius},
            float4{1.6f * static_cast<float>(census.alive) / static_cast<float>(particle_layout::explosion_particles - rotor_layout::rotor_disc_records), 0, 0, 0}};
    }
}
}
