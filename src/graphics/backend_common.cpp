#include "backend_common.hpp"

#include <cassert>
#include <cstring>

namespace pyroclast
{
std::uint32_t TimestampRegions::begin(const char* label, const char* fallback)
{
    if (count == max_gpu_regions) return no_region;
    labels[count] = label ? label : fallback;
    return count++;
}

GpuTimings gpu_timings(std::span<const std::uint64_t> values, std::uint64_t mask, double milliseconds_per_tick,
    const TimestampRegions& regions, std::uint64_t sample_id)
{
    assert(values.size() >= 4 + 2 * regions.count);
    const auto milliseconds = [mask, milliseconds_per_tick](std::uint64_t begin, std::uint64_t end) {
        return static_cast<double>((end - begin) & mask) * milliseconds_per_tick;
    };
    GpuTimings timings{milliseconds(values[0], values[1]), milliseconds(values[2], values[3]), sample_id};
    timings.frame_ms = milliseconds(values[0], values[3]);
    for (std::uint32_t region = 0; region < regions.count; ++region)
    {
        timings.regions[region] = {regions.labels[region],
            milliseconds(values[TimestampRegions::begin_query(region)], values[TimestampRegions::end_query(region)])};
    }
    timings.region_count = regions.count;
    return timings;
}

ReadbackData copy_readback(const std::byte* mapped, std::uint32_t size, std::uint64_t sample_id)
{
    assert(size <= max_readback_bytes);
    ReadbackData data;
    std::memcpy(data.bytes.data(), mapped, size);
    data.size = size;
    data.sample_id = sample_id;
    return data;
}
}
