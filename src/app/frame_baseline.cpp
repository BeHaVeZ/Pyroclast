#include "frame_baseline.hpp"

#include "frame_statistics.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>

namespace pyroclast
{
namespace
{
std::string json_string(const std::string& text)
{
    std::string quoted = "\"";
    for (const char character : text)
    {
        if (character == '"' || character == '\\') quoted += '\\';
        if (static_cast<unsigned char>(character) >= 0x20) quoted += character;
    }
    return quoted + '"';
}
}

std::string camera_json(const std::array<float, 6>& pose)
{
    char text[160];
    std::snprintf(text, sizeof(text), "[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]", static_cast<double>(pose[0]), static_cast<double>(pose[1]),
        static_cast<double>(pose[2]), static_cast<double>(pose[3]), static_cast<double>(pose[4]), static_cast<double>(pose[5]));
    return text;
}

void FrameBaseline::record(double milliseconds, std::optional<GpuTimings> gpu, std::optional<std::uint32_t> alive,
    const CoverageStatistics* coverage)
{
    if (!enabled_) return;
    if (warmup_ != 0)
    {
        --warmup_;
        return;
    }
    if (gpu && gpu->sample_id != last_gpu_sample_)
    {
        const bool same_regions = gpu_count_ == 0 || (gpu->region_count == region_count_ &&
            std::equal(region_labels_.begin(), region_labels_.begin() + region_count_, gpu->regions.begin(),
                [](const char* label, const GpuRegion& region) { return label == region.label; }));
        if (!same_regions)
        {
            enabled_ = false;
            SDL_Log("Sample stopped: the frame's GPU work changed while sampling.");
            return;
        }
        last_gpu_sample_ = gpu->sample_id;
        ++gpu_count_;
        compute_ms_ += gpu->compute_ms;
        draw_ms_ += gpu->draw_ms;
        region_count_ = gpu->region_count;
        for (std::uint32_t region = 0; region < gpu->region_count; ++region)
        {
            region_labels_[region] = gpu->regions[region].label;
            region_ms_[region] += gpu->regions[region].milliseconds;
        }
    }
    samples_[count_++] = milliseconds;
    if (count_ != samples_.size()) return;
    enabled_ = false;
    const auto cpu = summarize(samples_);
    const auto& sample = description_;
    const char* renderer = sample.renderer.c_str();
    if (sample.profile_tick && !sample.scenario.empty())
        SDL_Log("Fixed-state sample: scenario %s frozen at tick=%u, zero integration steps, VSync off; warmup=120, samples=600, resolution=1280x720, capacity=%u, view=%s, scene=%s, renderer=%s, build=%s.",
            sample.scenario.c_str(), *sample.profile_tick, sample.capacity, sample.view.c_str(), sample.scene_format.c_str(), renderer,
            build_configuration);
    else if (sample.profile_tick)
        SDL_Log("Fixed-state sample: tick=%u, zero integration steps, VSync off; warmup=120, samples=600, resolution=1280x720, explosions=%u, capacity=%u, view=%s, scene=%s, renderer=%s, build=%s.",
            *sample.profile_tick, sample.explosions, sample.capacity, sample.view.c_str(), sample.scene_format.c_str(), renderer,
            build_configuration);
    else SDL_Log("Explosion startup baseline: 600 samples after 120 warmup frames; 1280x720, VSync on, explosions=%u, capacity=%u, burst=%u, renderer=%s, build=%s.",
        sample.explosions, sample.capacity, sample.explosions * particle_layout::explosion_particles, renderer, build_configuration);
    SDL_Log("CPU resize/render call including fence/Present waits: average=%.4f ms, median=%.4f ms, p99=%.4f ms. Not GPU execution time.",
        cpu.average, cpu.median, cpu.p99);
    const double compute = gpu_count_ != 0 ? compute_ms_ / static_cast<double>(gpu_count_) : 0.0;
    const double draw = gpu_count_ != 0 ? draw_ms_ / static_cast<double>(gpu_count_) : 0.0;
    if (gpu_count_ != 0)
    {
        SDL_Log("GPU timestamp baseline: %zu samples; compute region average=%.6f ms, draw region average=%.6f ms (instrumented, includes barriers/clear).",
            gpu_count_, compute, draw);
    }

    std::string regions = "[";
    for (std::uint32_t region = 0; region < region_count_ && gpu_count_ != 0; ++region)
    {
        const double average = region_ms_[region] / static_cast<double>(gpu_count_);
        SDL_Log("GPU region %s: %.6f ms average.", region_labels_[region], average);
        char entry[160];
        std::snprintf(entry, sizeof(entry), "%s{\"label\":%s,\"ms\":%.6f}", region == 0 ? "" : ",",
            json_string(region_labels_[region]).c_str(), average);
        regions += entry;
    }
    regions += "]";

    const std::string tick = sample.profile_tick ? std::to_string(*sample.profile_tick) : "null";
    const std::string alive_particles = alive ? std::to_string(*alive) : "null";
    if (alive) SDL_Log("Alive particles at the end of the sample: %u.", *alive);
    std::string gpu_ms = "null";
    if (gpu_count_ != 0)
    {
        char summary[128];
        std::snprintf(summary, sizeof(summary), "{\"samples\":%zu,\"compute\":%.6f,\"draw\":%.6f,\"regions\":", gpu_count_, compute, draw);
        gpu_ms = summary + regions + "}";
    }
    std::string extra;
    if (!sample.scenario.empty()) extra += ",\"scenario\":" + json_string(sample.scenario);
    if (sample.camera) extra += ",\"camera\":" + camera_json(*sample.camera);
    if (coverage && coverage->samples() != 0) extra += "," + coverage->json();
    std::printf("{\"build\":\"%s\",\"renderer\":%s,\"gpu\":%s,\"explosions\":%u,\"capacity\":%u,\"profile_tick\":%s,"
        "\"vsync\":%s,\"smoke_resolution\":\"%s\",\"view\":%s,\"scene_format\":%s,\"preset\":%s,\"layer_mask\":%u,\"wind\":[%.2f,%.1f,%.2f]%s,"
        "\"width\":1280,\"height\":720,\"warmup_frames\":%zu,\"samples\":%zu,"
        "\"alive\":%s,\"cpu_ms\":{\"average\":%.4f,\"median\":%.4f,\"p99\":%.4f},\"gpu_ms\":%s}\n",
        build_configuration, json_string(sample.renderer).c_str(), json_string(sample.gpu).c_str(),
        sample.explosions, sample.capacity, tick.c_str(), sample.profile_tick ? "false" : "true",
        sample.half_resolution_smoke ? "half" : "full", json_string(sample.view).c_str(), json_string(sample.scene_format).c_str(),
        json_string(sample.preset).c_str(), sample.layer_mask, static_cast<double>(sample.wind.speed),
        static_cast<double>(sample.wind.direction_degrees), static_cast<double>(sample.wind.gustiness), extra.c_str(), warmup_frames,
        samples_.size(), alive_particles.c_str(), cpu.average, cpu.median, cpu.p99, gpu_ms.c_str());
    std::fflush(stdout);
}
}
