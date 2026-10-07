#pragma once

#include <cstdint>
#include <span>

namespace pyroclast::tools
{
struct ImageDifference
{
    std::uint64_t pixels = 0;
    std::uint64_t differing_pixels = 0;
    std::uint64_t beyond_one_level = 0;
    unsigned max_level = 0;
    double mean_level = 0;
    unsigned p99_level = 0;
};

// Both spans hold the same number of RGBA8 pixels.
ImageDifference compare_rgba(std::span<const std::uint8_t> first, std::span<const std::uint8_t> second);
}
