#include "frame_baseline.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <numeric>

namespace pyroclast
{
void FrameBaseline::record(double milliseconds)
{
    if (!enabled_) return;
    if (warmup_ != 0)
    {
        --warmup_;
        return;
    }
    samples_[count_++] = milliseconds;
    if (count_ != samples_.size()) return;
    enabled_ = false;
    const double average = std::accumulate(samples_.begin(), samples_.end(), 0.0) / samples_.size();
    std::sort(samples_.begin(), samples_.end());
    const double median = (samples_[299] + samples_[300]) * 0.5;
    SDL_Log("Explosion startup baseline: 600 samples after 120 warmup frames; 1280x720, VSync on, capacity=8192, burst=2048.");
    SDL_Log("CPU resize/render call including fence/Present waits: average=%.4f ms, median=%.4f ms, p99=%.4f ms. Not GPU execution time.",
        average, median, samples_[593]);
}
}
