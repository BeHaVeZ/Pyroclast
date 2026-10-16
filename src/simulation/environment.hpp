#pragma once

#include "range_props.hpp"
#include "terrain.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace pyroclast
{
// As the GPU reads it (terrain_layout.hlsli): bit b of mask_low (b < 32) or mask_high (b - 32) lists box b.
struct GpuGridCell
{
    std::uint32_t mask_low = 0;
    std::uint32_t mask_high = 0;
    float ceiling = 0;
    std::uint32_t reserved = 0;
};
static_assert(sizeof(GpuGridCell) == terrain_layout::environment_cell_bytes);

struct GpuPropBox
{
    float3 centre;
    float axis_cos = 1;
    float3 half_extent;
    float axis_sin = 0;
};
static_assert(sizeof(GpuPropBox) == terrain_layout::prop_box_stride && offsetof(GpuPropBox, axis_cos) == 12 &&
    offsetof(GpuPropBox, half_extent) == 16 && offsetof(GpuPropBox, axis_sin) == 28);

struct GpuEnvironmentFlags
{
    std::uint32_t open_low = 0;
    std::uint32_t open_high = 0;
    std::uint32_t box_count = 0;
    std::uint32_t reserved = 0;
};
static_assert(sizeof(GpuEnvironmentFlags) == terrain_layout::environment_bytes - terrain_layout::environment_flags_offset);

struct SurfaceHit
{
    float3 point;
    float3 normal;
    SurfaceType surface = SurfaceType::dirt;
    std::optional<std::uint32_t> prop;
};

// The CPU's queries decode the same bytes the GPU reads, so both see the same numbers.
class Environment final
{
public:
    static constexpr float empty_ceiling = -1e30f;

    struct Statistics
    {
        std::uint32_t props = 0;
        std::uint32_t boxes = 0;
        std::uint32_t occupied_cells = 0;
        std::uint32_t longest_list = 0;
        std::uint32_t bytes = 0;
    };

    Environment();
    const Terrain& terrain() const { return terrain_; }
    std::span<const Prop> props() const { return layout_.props; }
    std::span<const PropBox> boxes() const { return layout_.boxes; }
    std::span<const std::byte> bytes() const { return bytes_; }

    GpuGridCell cell(float x, float z) const;
    PropBox packed_box(std::uint32_t index) const;
    std::uint32_t box_count() const;
    bool inside(float3 point) const;
    // A box the ray starts in is ignored, and a prop wins a tie with the terrain.
    std::optional<SurfaceHit> raycast(const Ray& ray, float max_distance, bool props) const;
    float3 event_origin(const SurfaceHit& hit) const;
    Statistics statistics() const;

private:
    Terrain terrain_;
    RangeLayout layout_;
    std::vector<std::byte> bytes_;
};

// As shaders/environment.hlsli computes it; nullopt outside the grid.
struct GridIndex
{
    std::uint32_t column = 0;
    std::uint32_t row = 0;
};
std::optional<GridIndex> grid_index(float x, float z);
}
