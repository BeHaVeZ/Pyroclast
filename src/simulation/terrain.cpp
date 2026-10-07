#include "terrain.hpp"

#include <algorithm>
#include <cmath>

namespace pyroclast
{
namespace
{
float smoothstep(float edge0, float edge1, float x)
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

float generated_height(float x, float z)
{
    const float hills = 7.0f * std::sin(0.011f * x + 0.7f) * std::cos(0.013f * z - 0.3f) +
        3.5f * std::sin(0.023f * x - 0.019f * z + 1.1f) +
        1.6f * std::sin(0.051f * z + 0.4f) * std::cos(0.037f * x);
    const float range = smoothstep(70.0f, 170.0f, std::sqrt(x * x + z * z));
    return range * (hills + 6.0f);
}
}

Terrain::Terrain()
{
    heights_.resize(static_cast<std::size_t>(resolution) * resolution);
    for (std::uint32_t row = 0; row < resolution; ++row)
    {
        for (std::uint32_t column = 0; column < resolution; ++column)
        {
            heights_[static_cast<std::size_t>(row) * resolution + column] =
                generated_height(column * spacing - half_extent, row * spacing - half_extent);
        }
    }
}

float Terrain::height(float x, float z) const
{
    const float u = std::clamp((x + half_extent) / spacing, 0.0f, static_cast<float>(resolution - 1));
    const float v = std::clamp((z + half_extent) / spacing, 0.0f, static_cast<float>(resolution - 1));
    const auto column = std::min(static_cast<std::uint32_t>(u), resolution - 2);
    const auto row = std::min(static_cast<std::uint32_t>(v), resolution - 2);
    const float fx = u - static_cast<float>(column);
    const float fz = v - static_cast<float>(row);
    const auto at = [this](std::uint32_t c, std::uint32_t r) { return heights_[static_cast<std::size_t>(r) * resolution + c]; };
    const float near_row = at(column, row) + (at(column + 1, row) - at(column, row)) * fx;
    const float far_row = at(column, row + 1) + (at(column + 1, row + 1) - at(column, row + 1)) * fx;
    return near_row + (far_row - near_row) * fz;
}

float3 Terrain::normal(float x, float z) const
{
    const float dx = height(x + spacing, z) - height(x - spacing, z);
    const float dz = height(x, z + spacing) - height(x, z - spacing);
    return normalize(float3{-dx, 2 * spacing, -dz});
}

std::optional<float3> Terrain::raycast(const Ray& ray, float max_distance) const
{
    const float step = spacing * 0.5f;
    float previous = 0;
    for (float distance = 0; distance <= max_distance; distance += step)
    {
        const float3 point = ray.origin + ray.direction * distance;
        if (point.y <= height(point.x, point.z))
        {
            float low = previous;
            float high = distance;
            for (int iteration = 0; iteration < 16; ++iteration)
            {
                const float middle = 0.5f * (low + high);
                const float3 probe = ray.origin + ray.direction * middle;
                (probe.y <= height(probe.x, probe.z) ? high : low) = middle;
            }
            return ray.origin + ray.direction * high;
        }
        previous = distance;
    }
    return std::nullopt;
}
}
