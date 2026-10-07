#pragma once

#include <span>

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
}
