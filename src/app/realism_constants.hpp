#pragma once

#include "camera.hpp"
#include "frame_constants.hpp"
#include "scalability.hpp"
#include "../simulation/explosion_system.hpp"
#include "../simulation/particle_system.hpp"

namespace pyroclast
{
struct RealismUnits
{
    FrameUnits units;
    std::uint32_t clusters = 0;
};

RealismUnits fill_realism_constants(FrameConstants& constants, const ExplosionSystem& effects, std::uint32_t tick, std::uint32_t key_tick,
    const Camera& camera, EffectOrder order, float3 drift, const LingerSettings& linger);
FrameUnits count_units(const ExplosionSystem& effects, std::uint32_t tick);
}
