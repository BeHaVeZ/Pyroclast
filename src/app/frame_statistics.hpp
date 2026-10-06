#pragma once

#include <span>

namespace pyroclast
{
struct FrameStatistics
{
    double average;
    double median;
    double p99;
};

FrameStatistics summarize(std::span<double> samples);
}
