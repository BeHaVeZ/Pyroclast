#include "jet_constants.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace pyroclast
{
JetFrame prepare_jet(Jetcraft& jetcraft, const ExplosionSystem& effects, const Terrain& terrain, const WindSettings& wind, bool gusts,
    std::uint32_t mask, std::uint32_t tick, std::uint32_t steps, std::uint32_t drawn_tick)
{
    JetFrame frame;
    const auto& flight = jetcraft.flight();
    if (!flight) return frame;
    frame.exists = true;
    frame.step_count = std::min(steps, particle_layout::maximum_simulation_steps);
    for (std::uint32_t step = 0; step < frame.step_count; ++step)
    {
        const std::uint32_t at = tick + step + 1;
        const auto pose = jet_pose(*flight, at, terrain, wind, gusts, mask);
        frame.heads[step] = jet_head(pose);
        const auto& emission = frame.emissions[step] = jet_emission(*flight, at, terrain, wind, gusts, mask, jetcraft.history());
        float3 point = to_float3(pose.centre);
        if (emission.exhaust == 0 && emission.dust != 0)
        {
            const float2 middle{0.5f * (emission.dust_from.x + emission.dust_to.x), 0.5f * (emission.dust_from.y + emission.dust_to.y)};
            point = {middle.x, terrain.height(middle.x, middle.y) + 2, middle.y};
        }
        jetcraft.emissions().store(at, emission, point);
    }
    frame.pose = jet_pose(*flight, drawn_tick, terrain, wind, gusts, mask);
    if (frame.step_count == 0 || drawn_tick != tick + frame.step_count)
    {
        frame.heads[0] = jet_head(frame.pose);
        frame.drawn_head = 0;
    }
    else frame.drawn_head = frame.step_count - 1;
    frame.wake = jet_wake(*flight, drawn_tick, terrain, wind, gusts, mask, jetcraft.history());
    for (std::uint32_t step = 0; step < std::max(frame.step_count, 1u); ++step) include_head(frame.wake, frame.heads[step]);
    frame.key = to_float3(frame.pose.centre);
    frame.slot = effects.jet_slot();
    if (frame.slot)
    {
        const auto& slot = effects.slots()[*frame.slot];
        const bool smoke = ((mask >> particle_layout::layer_blast_smoke) & 1) != 0;
        const bool dust = ((mask >> particle_layout::layer_ground_dust) & 1) != 0;
        frame.census = jet_census(jetcraft.emissions(), slot.seed, slot.start_tick, slot.start_tick + slot.lifetime_ticks, drawn_tick, smoke, dust);
        if (frame.census.exhaust + frame.census.dust != 0) frame.key = to_float3(frame.census.mean_point);
    }
    return frame;
}

void fill_jet_constants(FrameConstants& constants, const JetFrame& frame)
{
    constants.jet_state = {};
    constants.jet_box = {};
    constants.jet_heads = {};
    constants.jet_knots = {};
    if (!frame.exists) return;
    const auto& wake = frame.wake;
    constants.jet_state = {wake.count, wake.joined ? 1u : 0u, frame.drawn_head, std::bit_cast<std::uint32_t>(static_cast<float>(frame.pose.fade))};
    constants.jet_box = {float4{wake.box_min.x, wake.box_min.y, wake.box_min.z, 0}, float4{wake.box_max.x, wake.box_max.y, wake.box_max.z, 0}};
    for (std::uint32_t step = 0; step < particle_layout::maximum_simulation_steps; ++step)
    {
        for (std::uint32_t word = 0; word < 4; ++word) constants.jet_heads[step * 4 + word] = frame.heads[step].words[word];
    }
    for (std::uint32_t knot = 0; knot < wake.count; ++knot)
    {
        for (std::uint32_t word = 0; word < 4; ++word) constants.jet_knots[knot * 4 + word] = wake.knots[knot].words[word];
    }
    constants.jet_births = {};
    constants.jet_dust_lines = {};
    for (std::uint32_t step = 0; step < frame.step_count; ++step)
    {
        const auto& emission = frame.emissions[step];
        constants.jet_births[step] = {emission.exhaust, emission.dust, static_cast<std::uint32_t>(std::lround(emission.half_width * 100)), 0};
        constants.jet_dust_lines[step] = {emission.dust_from.x, emission.dust_from.y, emission.dust_to.x, emission.dust_to.y};
    }
}
}
