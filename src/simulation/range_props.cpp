#include "range_props.hpp"

#include <cmath>
#include <initializer_list>

namespace pyroclast
{
namespace
{
struct Part
{
    float x = 0;
    float z = 0;
    float length = 0;
    float bottom = 0;
    float top = 0;
    float width = 0;
    bool open_bottom = false;
};

class LayoutBuilder
{
public:
    void add(std::string_view name, PropShape shape, PropMaterial material, float x, float z, float yaw, float shade,
        std::initializer_list<Part> parts, std::uint32_t seed = 0)
    {
        const double radians = static_cast<double>(yaw) * 3.14159265358979323846 / 180.0;
        const auto axis_cos = static_cast<float>(std::cos(radians));
        const auto axis_sin = static_cast<float>(std::sin(radians));
        const auto prop = static_cast<std::uint32_t>(layout_.props.size());
        layout_.props.push_back({name, shape, material, static_cast<std::uint32_t>(layout_.boxes.size()),
            static_cast<std::uint32_t>(parts.size()), shade, seed});
        for (const auto& part : parts)
        {
            PropBox box;
            box.axis_cos = axis_cos;
            box.axis_sin = axis_sin;
            box.centre = float3{x, (part.bottom + part.top) * 0.5f, z} + from_box(box, {part.x, 0, part.z});
            box.half_extent = {part.length * 0.5f, (part.top - part.bottom) * 0.5f, part.width * 0.5f};
            box.open_bottom = part.open_bottom;
            box.prop = prop;
            layout_.boxes.push_back(box);
        }
    }

    RangeLayout take() { return std::move(layout_); }

private:
    RangeLayout layout_;
};

constexpr Part barrier_foot{0, 0, 3.0f, -0.2f, 0.255f, 0.61f};
constexpr Part barrier_upper{0, 0, 3.0f, -0.2f, 0.81f, 0.43f};
}

RangeLayout range_layout()
{
    LayoutBuilder range;
    // Grounded parts reach 0.2 m (rocks 0.3 m) below the flat range, so no particle meets their bottom.
    range.add("blast wall A", PropShape::box, PropMaterial::concrete, -14, -54, 0, 1, {{0, 0, 10.0f, -0.2f, 3.0f, 0.4f}});
    range.add("blast wall B", PropShape::box, PropMaterial::concrete, -19.2f, -51.0f, 0, 1, {{0, 0, 0.4f, -0.2f, 3.0f, 6.4f}});
    range.add("hut", PropShape::box, PropMaterial::concrete, 8, -54, 0, 1,
        {{0, -2.35f, 6.0f, -0.2f, 2.6f, 0.3f},
            {-2.85f, 0, 0.3f, -0.2f, 2.6f, 4.4f},
            {2.85f, 0, 0.3f, -0.2f, 2.6f, 4.4f},
            {-1.85f, 2.35f, 2.3f, -0.2f, 2.6f, 0.3f},
            {1.85f, 2.35f, 2.3f, -0.2f, 2.6f, 0.3f},
            {0, 2.35f, 1.4f, 2.1f, 2.6f, 0.3f, true},
            {0, 0, 6.4f, 2.6f, 2.85f, 5.4f, true}});
    range.add("crate stack", PropShape::box, PropMaterial::wood, -2, -50, 20, 1,
        {{-0.62f, 0, 1.2f, -0.2f, 1.2f, 1.2f}, {0.62f, 0, 1.2f, -0.2f, 1.2f, 1.2f}, {0, 0, 1.2f, 1.2f, 2.4f, 1.2f}});
    range.add("truck", PropShape::box, PropMaterial::metal, 24, -52, -30, 1,
        {{-0.8f, 0, 4.4f, -0.2f, 2.6f, 2.4f}, {2.3f, 0, 1.8f, -0.2f, 2.2f, 2.3f}});
    for (const float x : {-37.6f, -34.5f, -31.4f, -28.3f, -25.2f})
    {
        range.add("jersey barrier", PropShape::jersey_barrier, PropMaterial::concrete, x, -50, 0, 1.3f, {barrier_foot, barrier_upper});
    }
    range.add("rock", PropShape::rock, PropMaterial::rock, -30, -57, 25, 1, {{0, 0, 2.6f, -0.3f, 1.3f, 2.0f}}, 1);
    range.add("rock", PropShape::rock, PropMaterial::rock, 36, -50, 70, 1, {{0, 0, 2.0f, -0.3f, 0.9f, 1.6f}}, 2);
    range.add("rock", PropShape::rock, PropMaterial::rock, 18, -60, 10, 1, {{0, 0, 1.6f, -0.3f, 0.7f, 1.4f}}, 3);
    range.add("tank", PropShape::box, PropMaterial::metal, 54, 26, 20, 1,
        {{0, 0, 6.6f, -0.2f, 1.4f, 2.8f}, {-0.6f, 0, 2.0f, 1.4f, 2.2f, 1.8f}});
    range.add("rock", PropShape::rock, PropMaterial::rock, 50, -42, -35, 1, {{0, 0, 2.2f, -0.3f, 1.1f, 1.8f}}, 4);
    range.add("burnt car", PropShape::box, PropMaterial::metal, -54, 28, 60, 0.6f,
        {{0, 0, 4.6f, -0.2f, 1.2f, 1.9f}, {-0.3f, 0, 2.4f, 1.2f, 2.0f, 1.7f}});
    for (const float x : {-31.55f, -28.45f})
    {
        range.add("jersey barrier", PropShape::jersey_barrier, PropMaterial::concrete, x, 50, 0, 1.3f, {barrier_foot, barrier_upper});
    }
    return range.take();
}

SurfaceType prop_surface(PropMaterial material)
{
    return material == PropMaterial::wood ? SurfaceType::dirt : SurfaceType::concrete;
}

std::string_view material_name(PropMaterial material)
{
    switch (material)
    {
    case PropMaterial::concrete: return "concrete";
    case PropMaterial::rock: return "rock";
    case PropMaterial::wood: return "wood";
    case PropMaterial::metal: return "metal";
    }
    return "unknown";
}

float3 to_box(const PropBox& box, float3 offset)
{
    return {offset.x * box.axis_cos + offset.z * box.axis_sin, offset.y, offset.z * box.axis_cos - offset.x * box.axis_sin};
}

float3 from_box(const PropBox& box, float3 local)
{
    return {local.x * box.axis_cos - local.z * box.axis_sin, local.y, local.x * box.axis_sin + local.z * box.axis_cos};
}
}
