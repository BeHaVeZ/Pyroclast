#pragma once

#include "particle_layout.hpp"

#include <cstdint>

namespace pyroclast::realism_layout
{
using uint = std::uint32_t;
using namespace particle_layout;
#include "../../shaders/realism_layout.hlsli"
// Every realism bit, 0-29. Declared in C++ only: declaring it in the HLSL reorders an entry's SPIR-V constants.
inline constexpr uint realism_full_mask = 0x3fffffff;
static_assert(realism_full_mask == ((1u << (brownout_mask_bit + 1)) - 1));
}

namespace pyroclast
{
constexpr bool realism_enabled(std::uint32_t mask)
{
    return ((mask >> realism_layout::realism_mask_bit) & 1) != 0;
}
}
