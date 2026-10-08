#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace pyroclast
{
struct FrameStatistics
{
    double average;
    double median;
    double p99;
    double p95;
    double max;
};

FrameStatistics summarize(std::span<double> samples);

class TickRate final
{
public:
    void add(double seconds, std::uint32_t tick);
    double rate() const;

private:
    std::array<std::pair<double, std::uint32_t>, 512> samples_{};
    std::size_t count_ = 0;
    std::size_t next_ = 0;
};
}
