#pragma once

#include "graphics_device.hpp"

#include <cstdint>

namespace pyroclast
{
// Per slot: compute begin and end, draw begin and end, then a begin/end pair per region.
inline constexpr std::uint32_t timestamp_queries_per_slot = 4 + 2 * max_gpu_regions;
inline constexpr std::uint32_t no_region = ~0u;

struct TimestampRegions
{
    std::array<const char*, max_gpu_regions> labels{};
    std::uint32_t count = 0;

    // The new region's index, or no_region once every region is used.
    std::uint32_t begin(const char* label, const char* fallback);
    static std::uint32_t begin_query(std::uint32_t region) { return 4 + 2 * region; }
    static std::uint32_t end_query(std::uint32_t region) { return 5 + 2 * region; }
};

// `values` are a completed slot's timestamps in ticks; `mask` keeps the valid bits of differences.
GpuTimings gpu_timings(std::span<const std::uint64_t> values, std::uint64_t mask, double milliseconds_per_tick,
    const TimestampRegions& regions, std::uint64_t sample_id);

ReadbackData copy_readback(const std::byte* mapped, std::uint32_t size, std::uint64_t sample_id);
}
