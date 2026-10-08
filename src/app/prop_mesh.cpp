#include "prop_mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace pyroclast
{
namespace
{
float unit_hash(std::uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return static_cast<float>(value & 0xffffu) / 65536.0f;
}

class MeshBuilder
{
public:
    MeshBuilder(std::vector<PropVertex>& vertices, const PropBox& frame, PropMaterial material, float shade)
        : vertices_(vertices), frame_(frame), material_(static_cast<std::uint32_t>(material)), shade_(shade)
    {
    }

    void polygon(std::span<const float3> local, float3 outward, float shade = 1)
    {
        std::array<float3, 8> points{};
        const auto count = std::min(local.size(), points.size());
        for (std::size_t index = 0; index < count; ++index) points[index] = frame_.centre + from_box(frame_, local[index]);
        auto normal = normalize(cross(points[1] - points[0], points[2] - points[0]));
        if (dot(normal, from_box(frame_, outward)) < 0)
        {
            std::reverse(points.begin(), points.begin() + static_cast<std::ptrdiff_t>(count));
            normal = -normal;
        }
        for (std::size_t index = 1; index + 1 < count; ++index)
        {
            for (const auto& point : {points[0], points[index], points[index + 1]})
            {
                vertices_.push_back({point, material_, normal, shade_ * shade});
            }
        }
    }

    void box()
    {
        const auto& h = frame_.half_extent;
        for (int axis = 0; axis < 3; ++axis)
        {
            for (const float sign : {-1.0f, 1.0f})
            {
                std::array<float3, 4> corners;
                for (int corner = 0; corner < 4; ++corner)
                {
                    const float u = corner == 1 || corner == 2 ? 1.0f : -1.0f;
                    const float v = corner >= 2 ? 1.0f : -1.0f;
                    corners[corner] = axis == 0 ? float3{sign * h.x, u * h.y, v * h.z}
                        : axis == 1             ? float3{v * h.x, sign * h.y, u * h.z}
                                                : float3{u * h.x, v * h.y, sign * h.z};
                }
                const float3 outward{axis == 0 ? sign : 0.0f, axis == 1 ? sign : 0.0f, axis == 2 ? sign : 0.0f};
                polygon(corners, outward);
            }
        }
    }

private:
    std::vector<PropVertex>& vertices_;
    PropBox frame_;
    std::uint32_t material_;
    float shade_;
};

void append_jersey_barrier(std::vector<PropVertex>& vertices, const Prop& prop, const PropBox& foot)
{
    MeshBuilder mesh(vertices, foot, prop.material, prop.shade);
    constexpr std::array<float2, 8> profile{{{-0.305f, -0.2f}, {0.305f, -0.2f}, {0.305f, 0.075f}, {0.215f, 0.255f},
        {0.075f, 0.81f}, {-0.075f, 0.81f}, {-0.215f, 0.255f}, {-0.305f, 0.075f}}};
    const float half_length = foot.half_extent.x;
    const float base = -foot.centre.y;
    const auto at = [&](std::size_t index, float x) { return float3{x, profile[index].y + base, profile[index].x}; };
    for (std::size_t index = 0; index < profile.size(); ++index)
    {
        const auto next = (index + 1) % profile.size();
        const float3 side[] = {at(index, -half_length), at(next, -half_length), at(next, half_length), at(index, half_length)};
        const float2 edge{profile[next].x - profile[index].x, profile[next].y - profile[index].y};
        mesh.polygon(side, {0, -edge.x, edge.y});
    }
    for (const float x : {-half_length, half_length})
    {
        const float3 lower[] = {at(0, x), at(1, x), at(2, x), at(3, x), at(6, x), at(7, x)};
        const float3 upper[] = {at(6, x), at(3, x), at(4, x), at(5, x)};
        mesh.polygon(lower, {x, 0, 0});
        mesh.polygon(upper, {x, 0, 0});
    }
}

void append_rock(std::vector<PropVertex>& vertices, const Prop& prop, const PropBox& box)
{
    MeshBuilder mesh(vertices, box, prop.material, prop.shade);
    const auto& h = box.half_extent;
    const float smallest = std::min({h.x, h.y, h.z});
    const auto sign = [](std::uint32_t corner, int axis) { return ((corner >> axis) & 1u) != 0 ? 1.0f : -1.0f; };
    const auto corner_point = [&](std::uint32_t corner) { return float3{sign(corner, 0) * h.x, sign(corner, 1) * h.y, sign(corner, 2) * h.z}; };
    std::array<float, 8> cut{};
    for (std::uint32_t corner = 0; corner < 8; ++corner) cut[corner] = smallest * (0.25f + 0.15f * unit_hash(prop.seed * 16 + corner));
    const auto facet_shade = [&](std::uint32_t facet) { return 0.82f + 0.3f * unit_hash(prop.seed * 16 + 8 + facet); };
    const auto towards = [&](std::uint32_t corner, std::uint32_t neighbour, float distance) {
        const auto from = corner_point(corner);
        const auto offset = corner_point(neighbour) - from;
        return from + offset * (distance / length(offset));
    };
    for (int axis = 0; axis < 3; ++axis)
    {
        for (const std::uint32_t side : {0u, 1u})
        {
            const int u = (axis + 1) % 3;
            const int v = (axis + 2) % 3;
            std::array<std::uint32_t, 4> ring{};
            constexpr std::array<std::uint32_t, 4> gray{0, 1, 3, 2};
            for (int index = 0; index < 4; ++index)
            {
                ring[index] = (side << axis) | ((gray[index] & 1u) << u) | ((gray[index] >> 1) << v);
            }
            std::array<float3, 8> octagon{};
            for (int index = 0; index < 4; ++index)
            {
                const auto corner = ring[index];
                octagon[2 * index] = towards(corner, ring[(index + 3) % 4], cut[corner]);
                octagon[2 * index + 1] = towards(corner, ring[(index + 1) % 4], cut[corner]);
            }
            const float3 outward{axis == 0 ? sign(side << axis, axis) : 0.0f, axis == 1 ? sign(side << axis, axis) : 0.0f,
                axis == 2 ? sign(side << axis, axis) : 0.0f};
            mesh.polygon(octagon, outward, facet_shade(static_cast<std::uint32_t>(axis * 2) + side));
        }
    }
    for (std::uint32_t corner = 0; corner < 8; ++corner)
    {
        const float3 facet[] = {towards(corner, corner ^ 1u, cut[corner]), towards(corner, corner ^ 2u, cut[corner]),
            towards(corner, corner ^ 4u, cut[corner])};
        mesh.polygon(facet, corner_point(corner), facet_shade(6 + corner));
    }
}
}

std::vector<PropVertex> build_prop_mesh(const Environment& environment)
{
    std::vector<PropVertex> vertices;
    const auto boxes = environment.boxes();
    for (const auto& prop : environment.props())
    {
        switch (prop.shape)
        {
        case PropShape::box:
            for (std::uint32_t index = prop.first_box; index < prop.first_box + prop.box_count; ++index)
            {
                MeshBuilder(vertices, boxes[index], prop.material, prop.shade).box();
            }
            break;
        case PropShape::jersey_barrier:
            append_jersey_barrier(vertices, prop, boxes[prop.first_box]);
            break;
        case PropShape::rock:
            append_rock(vertices, prop, boxes[prop.first_box]);
            break;
        }
    }
    return vertices;
}
}
