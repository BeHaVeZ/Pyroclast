#pragma once

#include "camera.hpp"
#include "frame_constants.hpp"
#include "scalability.hpp"
#include "../simulation/explosion_system.hpp"
#include "../render/particle_system.hpp"

namespace pyroclast
{
struct RealismUnits
{
    FrameUnits units;
    std::uint32_t clusters = 0;
};

// Draw units back to front by view depth; ties go to the earlier effect, then the slab, so a cluster draws right
// after its own slab. `drift` is the drift integral at the tick drawn.
RealismUnits fill_realism_constants(FrameConstants& constants, const ExplosionSystem& effects, std::uint32_t tick, std::uint32_t key_tick,
    const Camera& camera, EffectOrder order, float3 drift, const LingerSettings& linger, std::optional<float3> jet_key = std::nullopt, std::optional<float3> rotor_key = std::nullopt,
    std::optional<float3> disc_key = std::nullopt);
// The pall's shadow power (frame.residual.z) depends on the tick drawn alone, so a frozen frame equals the scenario's.
inline constexpr std::uint32_t pall_ease_in_ticks = 600;
inline constexpr std::uint32_t pall_ease_out_ticks = 1200;
float pall_shadow_power(const ExplosionSystem& effects, std::uint32_t tick);
FrameUnits count_units(const ExplosionSystem& effects, std::uint32_t tick);
}
