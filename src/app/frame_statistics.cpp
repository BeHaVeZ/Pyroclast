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

void TickRate::add(double seconds, std::uint32_t tick)
{
    samples_[next_] = {seconds, tick};
    next_ = (next_ + 1) % samples_.size();
    count_ = std::min(count_ + 1, samples_.size());
}

double TickRate::rate() const
{
    if (count_ < 2) return 0;
    const auto& newest = samples_[(next_ + samples_.size() - 1) % samples_.size()];
    auto oldest = newest;
    for (std::size_t back = 1; back < count_; ++back)
    {
        const auto& sample = samples_[(next_ + samples_.size() - 1 - back) % samples_.size()];
        if (newest.first - sample.first > 1.0) break;
        oldest = sample;
    }
    const double span = newest.first - oldest.first;
    return span > 0 ? static_cast<double>(newest.second - oldest.second) / span : 0.0;
}
}
