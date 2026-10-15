#include "frame_step.hpp"

#include "realism_constants.hpp"

namespace pyroclast
{
namespace
{
bool gusts_enabled(std::uint32_t mask)
{
    return ((mask >> particle_layout::gusts_mask_bit) & 1) != 0;
}
}

FrameStep begin_frame_step(SimulationClock& clock, ExplosionSystem& effects, const WindDrift& drift, Rotorcraft* rotorcraft,
    Jetcraft* jetcraft, const Terrain& terrain, const StepSettings& settings, double elapsed_seconds)
{
    const bool realism = realism_enabled(settings.layer_mask);
    const bool gusts = gusts_enabled(settings.layer_mask);
    FrameStep step;
    step.tick = clock.tick();
    const RealismStart realism_start{settings.layer_mask, settings.linger, drift.at(step.tick, settings.wind, gusts)};
    if (!realism)
    {
        rotorcraft = nullptr;
        jetcraft = nullptr;
    }
    const bool stepping = clock.stepping();
    if (rotorcraft && rotorcraft->helicopter() && rotorcraft->request_pending() && stepping &&
        step.tick >= rotorcraft->helicopter()->plan.list_tick)
    {
        const auto& helicopter = *rotorcraft->helicopter();
        effects.request_rotor(helicopter.seed, to_float3(flight_state(helicopter.plan, step.tick).position), rotorcraft->slot_end());
        rotorcraft->requested();
    }
    if (jetcraft && jetcraft->flight() && jetcraft->request_pending() && stepping && step.tick >= jetcraft->first_list_tick())
    {
        const auto& first = jetcraft->flight()->passes.front();
        effects.request_jet(jetcraft->seed(), to_float3(flight_state(first, first.list_tick).position), jetcraft->slot_end());
        jetcraft->requested();
    }
    effects.start(step.tick, settings.slot_policy, realism ? &realism_start : nullptr);
    step.steps = clock.advance(elapsed_seconds);
    if (rotorcraft)
    {
        const auto drift_at = [&drift, &settings, gusts](std::uint32_t at) { return drift.at(at, settings.wind, gusts); };
        step.rotor = prepare_rotor(*rotorcraft, effects, terrain, settings.wind, gusts, settings.layer_mask, step.tick, step.steps,
            step.drawn_tick(), drift_at);
    }
    if (jetcraft)
    {
        step.jet = prepare_jet(*jetcraft, effects, terrain, settings.wind, gusts, settings.layer_mask, step.tick, step.steps, step.drawn_tick());
    }
    return step;
}

void commit_frame_step(const FrameStep& step, SimulationClock& clock, WindDrift& drift, const StepSettings& settings)
{
    clock.commit(step.steps);
    drift.advance(clock.tick(), settings.wind, gusts_enabled(settings.layer_mask));
}
}
