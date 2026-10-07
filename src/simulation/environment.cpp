#include "environment.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstring>

namespace pyroclast
{
namespace
{
template <typename Record>
void store(std::vector<std::byte>& bytes, std::uint32_t offset, const Record& record)
{
    assert(offset + sizeof(Record) <= bytes.size());
    std::memcpy(bytes.data() + offset, &record, sizeof(Record));
}

template <typename Record>
Record load(std::span<const std::byte> bytes, std::uint32_t offset)
{
    Record record;
    assert(offset + sizeof(Record) <= bytes.size());
    std::memcpy(&record, bytes.data() + offset, sizeof(Record));
    return record;
}

float2 footprint_half(const PropBox& box)
{
    const float c = std::abs(box.axis_cos);
    const float s = std::abs(box.axis_sin);
    return {c * box.half_extent.x + s * box.half_extent.z, s * box.half_extent.x + c * box.half_extent.z};
}
}

std::optional<GridIndex> grid_index(float x, float z)
{
    using namespace terrain_layout;
    const float column = std::floor((x - environment_grid_min) * environment_cell_inverse);
    const float row = std::floor((z - environment_grid_min) * environment_cell_inverse);
    constexpr auto cells = static_cast<float>(environment_grid_cells);
    if (!(column >= 0 && column < cells && row >= 0 && row < cells)) return std::nullopt;
    return GridIndex{static_cast<std::uint32_t>(column), static_cast<std::uint32_t>(row)};
}

Environment::Environment() : layout_(range_layout())
{
    using namespace terrain_layout;
    assert(layout_.boxes.size() <= maximum_prop_boxes);
    bytes_.resize(environment_bytes);
    const auto heights = std::as_bytes(terrain_.heights());
    std::memcpy(bytes_.data(), heights.data(), heights.size());

    GpuEnvironmentFlags flags{};
    flags.box_count = static_cast<std::uint32_t>(layout_.boxes.size());
    for (std::uint32_t index = 0; index < flags.box_count; ++index)
    {
        const auto& box = layout_.boxes[index];
        store(bytes_, environment_boxes_offset + index * prop_box_stride, GpuPropBox{box.centre, box.axis_cos, box.half_extent, box.axis_sin});
        if (box.open_bottom) (index < 32 ? flags.open_low : flags.open_high) |= 1u << (index % 32);
    }
    store(bytes_, environment_flags_offset, flags);

    for (std::uint32_t row = 0; row < environment_grid_cells; ++row)
    {
        for (std::uint32_t column = 0; column < environment_grid_cells; ++column)
        {
            const float x0 = environment_grid_min + static_cast<float>(column) * environment_cell_size;
            const float z0 = environment_grid_min + static_cast<float>(row) * environment_cell_size;
            GpuGridCell cell{0, 0, empty_ceiling, 0};
            for (std::uint32_t index = 0; index < flags.box_count; ++index)
            {
                const auto& box = layout_.boxes[index];
                const auto half = footprint_half(box);
                const float reach_x = half.x + environment_reach;
                const float reach_z = half.y + environment_reach;
                if (box.centre.x - reach_x > x0 + environment_cell_size || box.centre.x + reach_x < x0 ||
                    box.centre.z - reach_z > z0 + environment_cell_size || box.centre.z + reach_z < z0)
                {
                    continue;
                }
                (index < 32 ? cell.mask_low : cell.mask_high) |= 1u << (index % 32);
                cell.ceiling = std::max(cell.ceiling, box.centre.y + box.half_extent.y + environment_reach);
            }
            store(bytes_, environment_grid_offset + (row * environment_grid_cells + column) * environment_cell_bytes, cell);
        }
    }
}

GpuGridCell Environment::cell(float x, float z) const
{
    using namespace terrain_layout;
    const auto index = grid_index(x, z);
    if (!index) return {0, 0, empty_ceiling, 0};
    return load<GpuGridCell>(bytes_, environment_grid_offset + (index->row * environment_grid_cells + index->column) * environment_cell_bytes);
}

PropBox Environment::packed_box(std::uint32_t index) const
{
    using namespace terrain_layout;
    assert(index < maximum_prop_boxes);
    const auto record = load<GpuPropBox>(bytes_, environment_boxes_offset + index * prop_box_stride);
    const auto flags = load<GpuEnvironmentFlags>(bytes_, environment_flags_offset);
    PropBox box;
    box.centre = record.centre;
    box.half_extent = record.half_extent;
    box.axis_cos = record.axis_cos;
    box.axis_sin = record.axis_sin;
    box.open_bottom = (((index < 32 ? flags.open_low : flags.open_high) >> (index % 32)) & 1u) != 0;
    box.prop = index < layout_.boxes.size() ? layout_.boxes[index].prop : 0;
    return box;
}

std::uint32_t Environment::box_count() const
{
    return load<GpuEnvironmentFlags>(bytes_, terrain_layout::environment_flags_offset).box_count;
}

bool Environment::inside(float3 point) const
{
    for (std::uint32_t index = 0; index < box_count(); ++index)
    {
        const auto box = packed_box(index);
        const auto local = to_box(box, point - box.centre);
        if (std::abs(local.x) < box.half_extent.x && std::abs(local.y) < box.half_extent.y && std::abs(local.z) < box.half_extent.z)
        {
            return true;
        }
    }
    return false;
}

std::optional<SurfaceHit> Environment::raycast(const Ray& ray, float max_distance, bool props) const
{
    std::optional<SurfaceHit> nearest;
    float nearest_distance = max_distance;
    if (const auto ground = terrain_.raycast(ray, max_distance))
    {
        nearest_distance = dot(*ground - ray.origin, ray.direction);
        nearest = SurfaceHit{*ground, terrain_.normal(ground->x, ground->z), Terrain::surface(ground->x, ground->z), std::nullopt};
    }
    if (!props) return nearest;
    for (std::uint32_t index = 0; index < box_count(); ++index)
    {
        const auto box = packed_box(index);
        const Ray local{to_box(box, ray.origin - box.centre), to_box(box, ray.direction)};
        const auto interval = box_interval(local, Box{-box.half_extent, box.half_extent});
        if (!interval || interval->entry <= 0 || interval->entry > nearest_distance) continue;
        if (interval->entry == nearest_distance && nearest && nearest->prop) continue;
        const auto hit = local.origin + local.direction * interval->entry;
        const float out[] = {std::abs(hit.x) / box.half_extent.x, std::abs(hit.y) / box.half_extent.y, std::abs(hit.z) / box.half_extent.z};
        const int axis = out[1] > out[0] ? (out[2] > out[1] ? 2 : 1) : (out[2] > out[0] ? 2 : 0);
        float3 face{};
        if (axis == 0) face.x = hit.x < 0 ? -1.0f : 1.0f;
        if (axis == 1) face.y = hit.y < 0 ? -1.0f : 1.0f;
        if (axis == 2) face.z = hit.z < 0 ? -1.0f : 1.0f;
        nearest_distance = interval->entry;
        nearest = SurfaceHit{ray.origin + ray.direction * interval->entry, from_box(box, face),
            prop_surface(layout_.props[box.prop].material), box.prop};
    }
    return nearest;
}

Environment::Statistics Environment::statistics() const
{
    using namespace terrain_layout;
    Statistics statistics{static_cast<std::uint32_t>(layout_.props.size()), box_count(), 0, 0, static_cast<std::uint32_t>(bytes_.size())};
    for (std::uint32_t cell = 0; cell < environment_grid_cells * environment_grid_cells; ++cell)
    {
        const auto record = load<GpuGridCell>(bytes_, environment_grid_offset + cell * environment_cell_bytes);
        const auto listed = static_cast<std::uint32_t>(std::popcount(record.mask_low) + std::popcount(record.mask_high));
        statistics.occupied_cells += listed > 0 ? 1 : 0;
        statistics.longest_list = std::max(statistics.longest_list, listed);
    }
    return statistics;
}
}
