#include "helicopter_mesh.hpp"
#include "../simulation/rotorcraft.hpp"

#include <array>

namespace pyroclast
{
namespace
{
using Corners = std::array<float3, 8>;

constexpr std::array<std::array<int, 4>, 6> faces{{{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}}};

void add_solid(std::vector<PropVertex>& vertices, const Corners& corners, float shade)
{
    float3 centre;
    for (const auto& corner : corners) centre += corner * 0.125f;
    for (const auto& face : faces)
    {
        std::array<float3, 4> quad{corners[face[0]], corners[face[1]], corners[face[2]], corners[face[3]]};
        float3 normal = cross(quad[1] - quad[0], quad[2] - quad[0]);
        if (dot(normal, normal) < 1e-12f) normal = cross(quad[2] - quad[0], quad[3] - quad[0]);
        const float3 middle = (quad[0] + quad[1] + quad[2] + quad[3]) * 0.25f;
        if (dot(normal, middle - centre) < 0)
        {
            std::swap(quad[1], quad[3]);
            normal = -normal;
        }
        normal = normalize(normal);
        for (const int index : {0, 1, 2, 0, 2, 3})
        {
            vertices.push_back({quad[index], static_cast<std::uint32_t>(PropMaterial::metal), normal, shade});
        }
    }
}

Corners frustum(float back_x, float3 back_centre, float back_height, float back_width, float front_x, float3 front_centre, float front_height,
    float front_width)
{
    Corners corners;
    for (int end = 0; end < 2; ++end)
    {
        const float x = end == 0 ? back_x : front_x;
        const float3 centre = end == 0 ? back_centre : front_centre;
        const float height = end == 0 ? back_height : front_height;
        const float width = end == 0 ? back_width : front_width;
        for (int corner = 0; corner < 4; ++corner)
        {
            const float y = centre.y + ((corner & 2) != 0 ? height : -height);
            const float z = centre.z + ((corner & 1) != 0 ? width : -width);
            corners[end * 4 + corner] = {x, y, z};
        }
    }
    return corners;
}

Corners box(float3 minimum, float3 maximum)
{
    const float3 centre = (minimum + maximum) * 0.5f;
    const float3 half = (maximum - minimum) * 0.5f;
    return frustum(minimum.x, {0, centre.y, centre.z}, half.y, half.z, maximum.x, {0, centre.y, centre.z}, half.y, half.z);
}
}

std::vector<PropVertex> build_helicopter_mesh()
{
    std::vector<PropVertex> vertices;
    vertices.reserve(600);
    const float floor = -3.6f;
    const float belt = -2.35f;
    const float roof = -1.6f;
    add_solid(vertices, box({-0.6f, floor, -1.2f}, {2.4f, belt, 1.2f}), 1.0f);
    add_solid(vertices, box({-0.6f, belt, -1.15f}, {2.4f, roof, 1.15f}), 0.3f);
    add_solid(vertices, frustum(2.4f, {0, -2.975f, 0}, 0.625f, 1.2f, 4.4f, {0, -3.15f, 0}, 0.35f, 0.55f), 1.0f);
    add_solid(vertices, frustum(2.4f, {0, -1.975f, 0}, 0.375f, 1.15f, 3.9f, {0, -2.55f, 0}, 0.2f, 0.6f), 0.3f);
    add_solid(vertices, frustum(-1.8f, {0, -1.2f, 0}, 0.4f, 0.6f, 1.2f, {0, -1.25f, 0}, 0.35f, 0.8f), 1.0f);
    add_solid(vertices, box({-0.15f, -0.8f, -0.15f}, {0.15f, -0.1f, 0.15f}), 0.5f);
    add_solid(vertices, box({-0.5f, -0.1f, -0.5f}, {0.5f, 0.1f, 0.5f}), 0.5f);
    add_solid(vertices, frustum(-9.6f, {0, -2.0f, 0}, 0.25f, 0.25f, -0.6f, {0, -2.4f, 0}, 0.5f, 0.6f), 1.0f);
    add_solid(vertices, frustum(-9.9f, {0, -0.9f, 0}, 0.9f, 0.08f, -8.6f, {0, -1.4f, 0}, 0.6f, 0.12f), 1.0f);
    add_solid(vertices, box({-8.9f, -2.12f, -1.6f}, {-8.1f, -2.02f, 1.6f}), 1.0f);
    add_solid(vertices, box({-10.05f, -2.15f, 0.1f}, {-9.75f, -1.85f, static_cast<float>(rotor::tail_disc_side) - 0.05f}), 0.5f);
    for (const float side : {-1.3f, 1.3f})
    {
        add_solid(vertices, box({-1.2f, -4.3f, side - 0.06f}, {3.3f, -4.18f, side + 0.06f}), 0.6f);
        for (const float x : {-0.2f, 1.9f})
        {
            const float inner = side < 0 ? -1.0f : 1.0f;
            add_solid(vertices, frustum(x - 0.06f, {0, -3.9f, (side + inner) * 0.5f}, 0.3f, 0.2f, x + 0.06f, {0, -3.9f, (side + inner) * 0.5f}, 0.3f, 0.2f),
                0.6f);
        }
    }
    return vertices;
}
}
