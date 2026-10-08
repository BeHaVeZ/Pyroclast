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
    auto& histogram = result.histogram;
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

std::vector<std::uint8_t> box_blur_rgba(std::span<const std::uint8_t> rgba, int width, int height, int radius)
{
    assert(width >= 0 && height >= 0 && radius >= 0 && rgba.size() == static_cast<std::size_t>(width) * height * 4);
    std::vector<std::uint8_t> blurred(rgba.begin(), rgba.end());
    if (radius == 0 || rgba.empty()) return blurred;
    const auto index = [width](int x, int y) { return (static_cast<std::size_t>(y) * width + x); };
    std::vector<std::uint32_t> rows(static_cast<std::size_t>(width) * height * 3);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            for (int channel = 0; channel < 3; ++channel)
            {
                std::uint32_t sum = 0;
                for (int dx = std::max(0, x - radius); dx <= std::min(width - 1, x + radius); ++dx) sum += rgba[index(dx, y) * 4 + channel];
                rows[index(x, y) * 3 + channel] = sum;
            }
        }
    }
    for (int y = 0; y < height; ++y)
    {
        const int top = std::max(0, y - radius);
        const int bottom = std::min(height - 1, y + radius);
        for (int x = 0; x < width; ++x)
        {
            const auto count = static_cast<std::uint32_t>((std::min(width - 1, x + radius) - std::max(0, x - radius) + 1) * (bottom - top + 1));
            for (int channel = 0; channel < 3; ++channel)
            {
                std::uint32_t sum = 0;
                for (int dy = top; dy <= bottom; ++dy) sum += rows[index(x, dy) * 3 + channel];
                blurred[index(x, y) * 4 + channel] = static_cast<std::uint8_t>((sum + count / 2) / count);
            }
        }
    }
    return blurred;
}

std::uint64_t ImageDifference::above(unsigned level) const
{
    std::uint64_t count = 0;
    for (unsigned index = level + 1; index < histogram.size(); ++index) count += histogram[index];
    return count;
}
}
