#pragma once

#include "../simulation/environment.hpp"

#include <cstdint>
#include <vector>

namespace pyroclast
{
struct PropVertex
{
    float3 position;
    std::uint32_t material = 0;
    float3 normal;
    float shade = 1;
};
static_assert(sizeof(PropVertex) == terrain_layout::prop_vertex_stride && offsetof(PropVertex, normal) == 16 &&
    offsetof(PropVertex, shade) == 28);

std::vector<PropVertex> build_prop_mesh(const Environment& environment);
}
