#include "frame_statistics.hpp"

#include "../graphics/particle_layout.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
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
    const auto worst = (count + 99) / 100;
    double tail = 0;
    for (auto index = count - worst; index < count; ++index) tail += samples[index];
    return {average, median, at_least(99), at_least(95), samples.back(), tail / static_cast<double>(worst)};
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

void CoverageStatistics::add(std::span<const std::uint32_t, 4> smoke_units, std::uint32_t light_units, std::uint32_t offscreen,
    double screen_pixels)
{
    const double scale = particle_layout::coverage_unit_pixels / std::max(screen_pixels, 1.0);
    double smoke = 0;
    for (std::size_t bin = 0; bin < bins_.size(); ++bin)
    {
        bins_[bin] += smoke_units[bin] * scale;
        smoke += smoke_units[bin] * scale;
    }
    smoke_.push_back(smoke);
    light_ += light_units * scale;
    offscreen_ += offscreen;
}

std::string CoverageStatistics::json() const
{
    if (smoke_.empty()) return {};
    auto sorted = smoke_;
    const auto summary = summarize(sorted);
    const double count = static_cast<double>(smoke_.size());
    char text[320];
    std::snprintf(text, sizeof(text),
        "\"coverage\":{\"samples\":%zu,\"smoke\":{\"average\":%.4f,\"p95\":%.4f,\"max\":%.4f},\"bins\":[%.4f,%.4f,%.4f,%.4f],"
        "\"light\":%.4f,\"offscreen\":%.1f}",
        smoke_.size(), summary.average, summary.p95, summary.max, bins_[0] / count, bins_[1] / count, bins_[2] / count, bins_[3] / count,
        light_ / count, offscreen_ / count);
    return text;
}
}
