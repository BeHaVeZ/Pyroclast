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
    const auto rank = (count * 99 + 99) / 100;
    return {average, median, samples[rank - 1]};
}
}
