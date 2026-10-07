#include "frame_statistics.hpp"

#include <algorithm>
#include <cassert>
#include <numeric>

namespace pyroclast
{
FrameStatistics summarize(std::span<double> samples)
{
    assert(!samples.empty());
    const auto count = samples.size();
    const double average = std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(count);
    std::sort(samples.begin(), samples.end());
    const double median = count % 2 == 1 ? samples[count / 2] : (samples[count / 2 - 1] + samples[count / 2]) * 0.5;
    const auto at_least = [&samples, count](std::size_t percent) { return samples[(count * percent + 99) / 100 - 1]; };
    return {average, median, at_least(99), at_least(95), samples.back()};
}
}
