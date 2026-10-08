#pragma once

#include "debug_views.hpp"
#include "../core/math.hpp"
#include "../graphics/graphics_device.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>

namespace pyroclast
{
struct ShowcaseMark
{
    enum Kind : std::uint32_t
    {
        ring = 0,
        draped_circle = 1,
        draped_rectangle = 2,
        wind_arrow = 3,
    };
    std::uint32_t kind = ring;
    std::uint32_t segments = 0;
    float radius = 0;
    float lift = 0;
    float3 centre;
    float half_width = 0;
    float3 axis{0, 1, 0};
    std::uint32_t side_segments = 0;
    float3 colour{1, 1, 1};
    float reserved = 0;
};
static_assert(sizeof(ShowcaseMark) == sizeof(DrawConstants) && offsetof(ShowcaseMark, centre) == 16 &&
    offsetof(ShowcaseMark, half_width) == 28 && offsetof(ShowcaseMark, axis) == 32 && offsetof(ShowcaseMark, side_segments) == 44 &&
    offsetof(ShowcaseMark, colour) == 48);

inline std::uint32_t mark_vertices(const ShowcaseMark& mark)
{
    if (mark.kind == ShowcaseMark::wind_arrow) return 8;
    return 2 * mark.segments + (mark.kind == ShowcaseMark::ring ? 2 : 0);
}

inline DrawConstants mark_constants(const ShowcaseMark& mark)
{
    return std::bit_cast<DrawConstants>(mark);
}
}
