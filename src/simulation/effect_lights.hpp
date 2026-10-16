#pragma once

#include "explosion_system.hpp"

#include <cstdint>
#include <span>

namespace pyroclast
{
// As the frame constants carry it: linear HDR intensity at its position, falling off as 1 / (1 + d^2), half at one metre.
struct PointLight
{
    float3 position;
    float radius = 0;
    float3 color;
    float source_radius = 0;
};

// The brightest first, equally bright ones in slot order; returns how many it wrote.
std::uint32_t select_lights(std::span<const EffectSlot> slots, std::span<const std::uint32_t> active, std::uint32_t tick,
    std::span<PointLight> lights, bool realism = false);
}
