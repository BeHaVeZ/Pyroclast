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
    if (profile_tick_)
        SDL_Log("Fixed-state sample: tick=%u, zero integration steps, VSync off; warmup=120, samples=600, resolution=1280x720, explosions=%u, capacity=%u, renderer=%s, build=%s.",
            *profile_tick_, explosions_, capacity_, renderer_, build_configuration);
    else SDL_Log("Explosion startup baseline: 600 samples after 120 warmup frames; 1280x720, VSync on, explosions=%u, capacity=%u, burst=%u, renderer=%s, build=%s.",
        explosions_, capacity_, explosions_ * particle_layout::explosion_particles, renderer_, build_configuration);
    SDL_Log("CPU resize/render call including fence/Present waits: average=%.4f ms, median=%.4f ms, p99=%.4f ms. Not GPU execution time.",
        average, median, samples_[593]);
    if (gpu_count_ != 0)
    {
        SDL_Log("GPU timestamp baseline: %zu samples; compute region average=%.6f ms, draw region average=%.6f ms (instrumented, includes barriers/clear).",
            gpu_count_, compute_ms_ / gpu_count_, draw_ms_ / gpu_count_);
    }
}
}
