#include "image_compare.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>

namespace pyroclast::tools
{
ImageDifference compare_rgba(std::span<const std::uint8_t> first, std::span<const std::uint8_t> second)
{
    assert(first.size() == second.size() && first.size() % 4 == 0);
    ImageDifference result;
    result.pixels = first.size() / 4;
    if (result.pixels == 0) return result;
    std::array<std::uint64_t, 256> histogram{};
    std::uint64_t total = 0;
    for (std::size_t offset = 0; offset < first.size(); offset += 4)
    {
        unsigned largest = 0;
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            const auto level = static_cast<unsigned>(std::abs(int{first[offset + channel]} - int{second[offset + channel]}));
            total += level;
            largest = std::max(largest, level);
        }
        ++histogram[largest];
        result.differing_pixels += largest != 0;
        result.beyond_one_level += largest > 1;
        result.max_level = std::max(result.max_level, largest);
    }
    result.mean_level = static_cast<double>(total) / static_cast<double>(result.pixels * 3);
    const std::uint64_t rank = (result.pixels * 99 + 99) / 100;
    std::uint64_t seen = 0;
    for (unsigned level = 0; level < histogram.size(); ++level)
    {
        seen += histogram[level];
        if (seen >= rank)
        {
            result.p99_level = level;
            break;
        }
    }
    return result;
}
}
