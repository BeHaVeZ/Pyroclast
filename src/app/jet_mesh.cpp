#include "jet_mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

namespace pyroclast
{
namespace
{
constexpr float pi = 3.14159265f;

float3 lerp(float3 a, float3 b, float t)
{
    return a + (b - a) * t;
}

float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

void push(std::vector<PropVertex>& vertices, float3 position, float3 normal, JetMaterial material, float shade)
{
    vertices.push_back({position, static_cast<std::uint32_t>(material), normal, shade});
}

void add_grid(std::vector<PropVertex>& vertices, int rows, int columns, bool ring, const std::function<float3(int, int)>& point,
    const std::function<float3(int, int)>& inside, const std::function<JetMaterial(int, int)>& material, float shade)
{
    const auto at = [&](int row, int column) {
        row = std::clamp(row, 0, rows - 1);
        column = ring ? (column + columns) % columns : std::clamp(column, 0, columns - 1);
        return point(row, column);
    };
    const auto normal_at = [&](int row, int column) {
        const float3 here = at(row, column);
        float3 normal = cross(at(row + 1, column) - at(row - 1, column), at(row, column + 1) - at(row, column - 1));
        const float3 outward = here - inside(row, ring ? (column + columns) % columns : std::clamp(column, 0, columns - 1));
        if (dot(normal, normal) < 1e-12f) normal = outward;
        if (dot(normal, normal) < 1e-12f) return float3{0, 1, 0};
        if (dot(normal, outward) < 0) normal = -normal;
        return normalize(normal);
    };
    const int spans = ring ? columns : columns - 1;
    for (int row = 0; row + 1 < rows; ++row)
    {
        for (int column = 0; column < spans; ++column)
        {
            const int next = ring ? (column + 1) % columns : column + 1;
            const auto chosen = material(row, column);
            const std::array<std::array<int, 2>, 6> corners{{{row, column}, {row + 1, column}, {row + 1, next}, {row, column}, {row + 1, next}, {row, next}}};
            for (const auto& corner : corners) push(vertices, at(corner[0], corner[1]), normal_at(corner[0], corner[1]), chosen, shade);
        }
    }
}

void add_fan(std::vector<PropVertex>& vertices, const std::vector<float3>& outline, float3 normal, JetMaterial material, float shade)
{
    float3 centre;
    for (const auto& point : outline) centre += point * (1.0f / static_cast<float>(outline.size()));
    for (std::size_t index = 0; index < outline.size(); ++index)
    {
        push(vertices, centre, normal, material, shade);
        push(vertices, outline[index], normal, material, shade);
        push(vertices, outline[(index + 1) % outline.size()], normal, material, shade);
    }
}

struct Station
{
    float x;
    float half_width;
    float half_height;
    float centre;
};

float3 section_point(const Station& station, float angle, float exponent, float z_offset = 0)
{
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const float y = std::copysign(std::pow(std::abs(s), 2 / exponent), s);
    const float z = std::copysign(std::pow(std::abs(c), 2 / exponent), c);
    return {station.x, station.centre + station.half_height * y, z_offset + station.half_width * z};
}

void add_body(std::vector<PropVertex>& vertices, const std::vector<Station>& stations, int around, float exponent, float z_offset,
    const std::function<JetMaterial(int, float)>& material, float shade)
{
    add_grid(
        vertices, static_cast<int>(stations.size()), around, true,
        [&](int row, int column) { return section_point(stations[row], 2 * pi * column / around, exponent, z_offset); },
        [&](int row, int) { return float3{stations[row].x, stations[row].centre, z_offset}; },
        [&](int row, int column) { return material(row, 2 * pi * column / around); }, shade);
}

void add_panel(std::vector<PropVertex>& vertices, float3 root, float3 tip, float root_chord, float tip_chord, float root_thickness, float tip_thickness,
    float3 thickness_axis, int span_rows, JetMaterial material, float shade)
{
    constexpr std::array<float, 7> chord_points{1.0f, 0.8f, 0.55f, 0.3f, 0.12f, 0.03f, 0.0f};
    std::vector<std::pair<float, float>> loop;
    for (const float xi : chord_points) loop.push_back({xi, 1.0f});
    for (int index = static_cast<int>(chord_points.size()) - 2; index >= 0; --index) loop.push_back({chord_points[index], -1.0f});
    const int columns = static_cast<int>(loop.size());
    const auto leading = [&](int row) { return lerp(root, tip, static_cast<float>(row) / (span_rows - 1)); };
    const auto chord = [&](int row) { return lerp(root_chord, tip_chord, static_cast<float>(row) / (span_rows - 1)); };
    const auto thickness = [&](int row) { return lerp(root_thickness, tip_thickness, static_cast<float>(row) / (span_rows - 1)); };
    const auto point = [&](int row, int column) {
        const auto [xi, side] = loop[column];
        const float half = 0.5f * thickness(row) * 4 * xi * (1 - xi);
        return leading(row) + float3{-xi * chord(row), 0, 0} + thickness_axis * (side * half);
    };
    add_grid(
        vertices, span_rows, columns, false, point, [&](int row, int) { return leading(row) + float3{-0.5f * chord(row), 0, 0}; },
        [material](int, int) { return material; }, shade);
    std::vector<float3> outline;
    for (int column = 0; column + 1 < columns; ++column) outline.push_back(point(span_rows - 1, column));
    add_fan(vertices, outline, normalize(tip - root), material, shade);
}

void add_plate(std::vector<PropVertex>& vertices, const std::vector<float3>& outline, float thickness, JetMaterial material, float shade)
{
    std::vector<float3> top;
    std::vector<float3> bottom;
    for (const auto& point : outline)
    {
        top.push_back(point + float3{0, 0.5f * thickness, 0});
        bottom.push_back(point - float3{0, 0.5f * thickness, 0});
    }
    add_fan(vertices, top, {0, 1, 0}, material, shade);
    add_fan(vertices, bottom, {0, -1, 0}, material, shade);
    float3 centre;
    for (const auto& point : outline) centre += point * (1.0f / static_cast<float>(outline.size()));
    for (std::size_t index = 0; index < outline.size(); ++index)
    {
        const std::size_t next = (index + 1) % outline.size();
        float3 normal = cross(outline[next] - outline[index], float3{0, 1, 0});
        if (dot(normal, outline[index] - centre) < 0) normal = -normal;
        normal = normalize(normal);
        for (const auto& corner : {top[index], bottom[index], bottom[next], top[index], bottom[next], top[next]}) push(vertices, corner, normal, material, shade);
    }
}
}

std::vector<PropVertex> build_jet_mesh()
{
    std::vector<PropVertex> vertices;
    vertices.reserve(6000);
    const std::vector<Station> fuselage{{7.6f, 0.0f, 0.0f, -0.05f}, {7.25f, 0.13f, 0.13f, -0.05f}, {6.8f, 0.27f, 0.26f, -0.04f},
        {6.0f, 0.44f, 0.42f, -0.02f}, {5.0f, 0.58f, 0.56f, 0.01f}, {4.0f, 0.66f, 0.66f, 0.04f}, {3.0f, 0.74f, 0.71f, 0.05f},
        {2.0f, 0.84f, 0.72f, 0.02f}, {1.0f, 0.95f, 0.72f, -0.03f}, {0.0f, 1.0f, 0.70f, -0.05f}, {-1.5f, 1.0f, 0.68f, -0.05f},
        {-3.0f, 0.95f, 0.65f, -0.02f}, {-4.5f, 0.85f, 0.60f, 0.0f}, {-5.8f, 0.70f, 0.55f, 0.0f}, {-6.6f, 0.58f, 0.52f, 0.0f},
        {-7.0f, 0.53f, 0.50f, 0.0f}, {-7.4f, 0.48f, 0.48f, 0.0f}};
    add_body(vertices, fuselage, 28, 2.4f, 0, [&fuselage](int row, float angle) {
        const float x = fuselage[row].x;
        if (x <= -6.6f) return JetMaterial::metal;
        if (x >= 7.2f) return JetMaterial::dark;
        if (x >= 4.9f && x <= 6.4f && std::sin(angle) > 0.75f) return JetMaterial::dark;
        return JetMaterial::paint;
    }, 1.0f);
    {
        std::vector<float3> glow;
        for (int index = 0; index < 20; ++index)
        {
            const float angle = 2 * pi * index / 20;
            glow.push_back({-7.33f, 0.40f * std::sin(angle), 0.40f * std::cos(angle)});
        }
        add_fan(vertices, glow, {-1, 0, 0}, JetMaterial::glow, 1.0f);
        add_grid(
            vertices, 2, 20, true,
            [](int row, int column) {
                const float angle = 2 * pi * column / 20;
                const float radius = row == 0 ? 0.48f : 0.40f;
                return float3{row == 0 ? -7.4f : -7.33f, radius * std::sin(angle), radius * std::cos(angle)};
            },
            [](int, int) { return float3{-7.0f, 0, 0}; }, [](int, int) { return JetMaterial::metal; }, 0.55f);
    }
    {
        const float3 centre{3.3f, 0.48f, 0};
        const float3 axes{1.9f, 0.62f, 0.52f};
        add_grid(
            vertices, 13, 13, false,
            [centre, axes](int row, int column) {
                const float xi = -1 + 2.0f * row / 12;
                const float phi = pi * column / 12;
                const float ring = std::sqrt(std::max(0.0f, 1 - xi * xi));
                return centre + float3{axes.x * xi, axes.y * ring * std::sin(phi), axes.z * ring * std::cos(phi)};
            },
            [centre](int, int) { return centre; }, [](int, int) { return JetMaterial::canopy; }, 1.0f);
    }
    {
        const std::vector<Station> intake{{2.75f, 0.46f, 0.30f, -0.92f}, {2.0f, 0.47f, 0.31f, -0.92f}, {0.8f, 0.45f, 0.30f, -0.85f},
            {-0.6f, 0.38f, 0.24f, -0.68f}};
        add_body(vertices, intake, 20, 3.0f, 0, [](int, float) { return JetMaterial::paint; }, 1.0f);
        std::vector<float3> mouth;
        for (int index = 0; index < 20; ++index) mouth.push_back(section_point({2.7f, 0.42f, 0.26f, -0.92f}, 2 * pi * index / 20, 3.0f));
        add_fan(vertices, mouth, {1, 0, 0}, JetMaterial::dark, 1.0f);
    }
    for (const float side : {-1.0f, 1.0f})
    {
        add_panel(vertices, {1.2f, -0.15f, side * 0.85f}, {-2.4f, -0.15f, side * 5.0f}, 4.6f, 1.3f, 0.24f, 0.07f, {0, 1, 0}, 5,
            JetMaterial::paint, 1.0f);
        add_plate(vertices, {{4.3f, -0.12f, side * 0.62f}, {0.8f, -0.12f, side * 1.3f}, {0.6f, -0.12f, side * 0.85f}}, 0.06f, JetMaterial::paint,
            1.0f);
        add_body(vertices, {{-0.6f, 0.0f, 0.0f, -0.15f}, {-0.9f, 0.06f, 0.06f, -0.15f}, {-1.1f, 0.065f, 0.065f, -0.15f}, {-3.9f, 0.065f, 0.065f, -0.15f}},
            10, 2.0f, side * 5.14f, [](int, float) { return JetMaterial::paint; }, 1.55f);
        add_panel(vertices, {-1.6f, -0.15f, side * 4.98f}, {-1.6f, -0.15f, side * 5.1f}, 2.0f, 2.0f, 0.05f, 0.05f, {0, 1, 0}, 2, JetMaterial::metal,
            0.8f);
        add_panel(vertices, {-5.0f, -0.2f, side * 0.95f}, {-6.5f, -0.58f, side * 3.1f}, 2.3f, 0.95f, 0.12f, 0.04f, {0, 1, 0}, 3, JetMaterial::paint,
            1.0f);
        add_panel(vertices, {-4.9f, -0.55f, side * 0.55f}, {-5.6f, -1.15f, side * 0.71f}, 1.5f, 0.8f, 0.06f, 0.03f, {0, 0, 1}, 2,
            JetMaterial::paint, 1.0f);
    }
    add_panel(vertices, {-3.5f, 0.5f, 0}, {-6.0f, 3.4f, 0}, 3.4f, 1.2f, 0.18f, 0.05f, {0, 0, 1}, 4, JetMaterial::paint, 1.0f);
    return vertices;
}
}
