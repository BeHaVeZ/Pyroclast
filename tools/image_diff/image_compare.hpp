#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

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
    std::array<std::uint64_t, 256> histogram{};
    std::uint64_t above(unsigned level) const;
};

// Both spans hold the same number of RGBA8 pixels.
ImageDifference compare_rgba(std::span<const std::uint8_t> first, std::span<const std::uint8_t> second);

std::vector<std::uint8_t> box_blur_rgba(std::span<const std::uint8_t> rgba, int width, int height, int radius);
}
