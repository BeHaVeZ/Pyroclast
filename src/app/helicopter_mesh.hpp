#pragma once

#include "prop_mesh.hpp"

#include <vector>

namespace pyroclast
{
// Body space: x forward, y up, z right, the origin at the main rotor's hub; triangles counter-clockwise from outside.
std::vector<PropVertex> build_helicopter_mesh();
}
