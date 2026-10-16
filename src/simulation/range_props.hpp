#pragma once

#include "explosion_presets.hpp"
#include "terrain.hpp"
#include "../core/math.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace pyroclast
{
enum class PropMaterial : std::uint32_t
{
    concrete = terrain_layout::prop_concrete,
    rock = terrain_layout::prop_rock,
    wood = terrain_layout::prop_wood,
    metal = terrain_layout::prop_metal,
};
static_assert(static_cast<std::uint32_t>(PropMaterial::metal) == 3);

enum class PropShape : std::uint32_t
{
    box,
    jersey_barrier,
    rock,
};

// Upright, turned about +Y: local +x is (axis_cos, 0, axis_sin), as in shaders/environment.hlsli.
struct PropBox
{
    float3 centre;
    float3 half_extent;
    float axis_cos = 1;
    float axis_sin = 0;
    bool open_bottom = false;
    std::uint32_t prop = 0;
};

struct Prop
{
    std::string_view name;
    PropShape shape = PropShape::box;
    PropMaterial material = PropMaterial::concrete;
    std::uint32_t first_box = 0;
    std::uint32_t box_count = 0;
    float shade = 1;
    std::uint32_t seed = 0;
};

struct RangeLayout
{
    std::vector<Prop> props;
    std::vector<PropBox> boxes;
};

// Every prop stands at least 6 m from every scripted explosion but the range scenario's (environment_tests checks).
RangeLayout range_layout();
SurfaceType prop_surface(PropMaterial material);
std::string_view material_name(PropMaterial material);

float3 to_box(const PropBox& box, float3 offset);
float3 from_box(const PropBox& box, float3 local);
}
