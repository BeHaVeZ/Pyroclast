#pragma once

#include "terrain.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace pyroclast
{
struct GpuGridCell
{
    std::uint32_t mask_low = 0;
    std::uint32_t mask_high = 0;
    float ceiling = 0;
    std::uint32_t reserved = 0;
};
static_assert(sizeof(GpuGridCell) == terrain_layout::environment_cell_bytes);

class Environment final
{
public:
    static constexpr float empty_ceiling = -1e30f;

    Environment();
    const Terrain& terrain() const { return terrain_; }
    std::span<const std::byte> bytes() const { return bytes_; }

private:
    Terrain terrain_;
    std::vector<std::byte> bytes_;
};
}
