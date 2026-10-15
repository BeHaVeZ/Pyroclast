#pragma once

#include "prop_mesh.hpp"

#include <cstdint>
#include <vector>

namespace pyroclast
{
enum class JetMaterial : std::uint32_t
{
    paint = 0,
    dark = 1,
    canopy = 2,
    metal = 3,
    glow = 4,
};

std::vector<PropVertex> build_jet_mesh();
}
