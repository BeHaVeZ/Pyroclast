#include "jet_constants.hpp"

#include <algorithm>
#include <bit>

namespace pyroclast
{
JetFrame prepare_jet(Jetcraft& jetcraft, const Terrain& terrain, const WindSettings& wind, bool gusts, std::uint32_t mask, std::uint32_t tick,
    std::uint32_t steps, std::uint32_t drawn_tick)
{
    JetFrame frame;
    const auto& flight = jetcraft.flight();
    if (!flight) return frame;
    frame.exists = true;
    frame.step_count = std::min(steps, particle_layout::maximum_simulation_steps);
    for (std::uint32_t step = 0; step < frame.step_count; ++step)
    {
        frame.heads[step] = jet_head(jet_pose(*flight, tick + step + 1, terrain, wind, gusts, mask));
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
}
}
