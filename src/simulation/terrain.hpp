#pragma once

#include "../core/math.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace pyroclast
{
namespace terrain_layout
{
using uint = std::uint32_t;
#include "../../shaders/terrain_layout.hlsli"
}

class Terrain final
{
public:
    static constexpr std::uint32_t resolution = terrain_layout::terrain_resolution;
    // The shared header's floats are const, not constexpr, so these are too.
    static inline const float spacing = terrain_layout::terrain_spacing;
    static inline const float half_extent = (resolution - 1) * spacing * 0.5f;

    Terrain();
    float height(float x, float z) const;
    float3 normal(float x, float z) const;
    std::optional<float3> raycast(const Ray& ray, float max_distance) const;
    // Row-major: sample (column, row) at index row * resolution + column, at x = column * spacing - half_extent.
    std::span<const float> heights() const { return heights_; }

private:
    std::vector<float> heights_;
    Box bounds_;
};
}
