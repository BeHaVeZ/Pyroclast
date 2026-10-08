#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace pyroclast
{
struct FrameStatistics
{
    double average;
    double median;
    double p99;
    double p95;
    double max;
    double worst_1_percent = 0;
};

FrameStatistics summarize(std::span<double> samples);

class CoverageStatistics final
{
public:
    void add(std::span<const std::uint32_t, 4> smoke_units, std::uint32_t light_units, std::uint32_t offscreen, double screen_pixels);
    std::size_t samples() const { return smoke_.size(); }
    std::string json() const;

private:
    std::vector<double> smoke_;
    std::array<double, 4> bins_{};
    double light_ = 0;
    double offscreen_ = 0;
};

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
