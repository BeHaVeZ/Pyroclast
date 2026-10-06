#include "frame_baseline.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <numeric>

namespace pyroclast
{
void FrameBaseline::record(double milliseconds, std::optional<GpuTimings> gpu)
{
    if (!enabled_) return;
    if (warmup_ != 0)
    {
        --warmup_;
        return;
    }
    if (gpu && gpu->sample_id != last_gpu_sample_)
    {
        last_gpu_sample_ = gpu->sample_id;
        ++gpu_count_;
        compute_ms_ += gpu->compute_ms;
        draw_ms_ += gpu->draw_ms;
    }
    samples_[count_++] = milliseconds;
    if (count_ != samples_.size()) return;
    enabled_ = false;
    const double average = std::accumulate(samples_.begin(), samples_.end(), 0.0) / samples_.size();
    std::sort(samples_.begin(), samples_.end());
    const double median = (samples_[299] + samples_[300]) * 0.5;
    SDL_Log("Explosion startup baseline: 600 samples after 120 warmup frames; 1280x720, VSync on, capacity=8192, burst=2560 (2048 emissive + 512 smoke).");
    SDL_Log("CPU resize/render call including fence/Present waits: average=%.4f ms, median=%.4f ms, p99=%.4f ms. Not GPU execution time.",
        average, median, samples_[593]);
    if (gpu_count_ != 0)
    {
        SDL_Log("GPU timestamp baseline: %zu samples; compute region average=%.6f ms, draw region average=%.6f ms (instrumented, includes barriers/clear).",
            gpu_count_, compute_ms_ / gpu_count_, draw_ms_ / gpu_count_);
    }
}
}
