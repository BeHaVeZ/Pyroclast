#pragma once

#include "explosion_system.hpp"

#include <cstdint>
#include <span>

namespace pyroclast
{
struct PointLight
{
    float3 position;
    float radius = 0;
    float3 color;
    float source_radius = 0;
};

std::uint32_t select_lights(std::span<const EffectSlot> slots, std::span<const std::uint32_t> active, std::uint32_t tick,
    std::span<PointLight> lights, bool realism = false);
}
