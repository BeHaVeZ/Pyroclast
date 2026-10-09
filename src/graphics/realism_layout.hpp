#pragma once

#include "particle_layout.hpp"

#include <cstdint>

namespace pyroclast::realism_layout
{
using uint = std::uint32_t;
using namespace particle_layout;
#include "../../shaders/realism_layout.hlsli"
}

namespace pyroclast
{
constexpr bool realism_enabled(std::uint32_t mask)
{
    return ((mask >> realism_layout::realism_mask_bit) & 1) != 0;
}
}
