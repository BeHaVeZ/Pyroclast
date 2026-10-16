#pragma once

#include "prop_mesh.hpp"

#include <cstdint>
#include <vector>

namespace pyroclast
{
// Kept in step with shaders/jet.hlsl.
enum class JetMaterial : std::uint32_t
{
    paint = 0,
    dark = 1,
    canopy = 2,
    metal = 3,
    glow = 4,
};

// Body space: x forward, y up, z right, the origin at the jet's centre; triangles counter-clockwise from outside.
std::vector<PropVertex> build_jet_mesh();
}
