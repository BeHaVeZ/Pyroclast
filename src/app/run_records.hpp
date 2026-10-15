#pragma once

#include "command_line.hpp"
#include "counter_log.hpp"
#include "frame_statistics.hpp"
#include "sandbox.hpp"
#include "../simulation/explosion_scenarios.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pyroclast
{
std::string statistics_json(const std::optional<FrameStatistics>& statistics);

const char* pose_name(const ApplicationOptions& options);

std::string view_keys(float vertical_fov, bool borderless);

class ScenarioRun final
{
public:
    ScenarioRun(Scenario scenario, std::string benchmark, std::uint32_t warmup_frames) :
        scenario_(std::move(scenario)), benchmark_(std::move(benchmark)), warmup_left_(warmup_frames)
    {
    }
    explicit ScenarioRun(Scenario scenario) : ScenarioRun(std::move(scenario), {}, 0) {}
    bool finished(const Sandbox& sandbox) const { return sandbox.clock.tick() >= scenario_.duration_ticks; }
    void invalidate(const char* reason)
    {
        if (valid_)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Scenario measurement invalidated: %s.", reason);
            invalid_reason_ = reason;
        }
        valid_ = false;
    }
    const std::string& benchmark() const { return benchmark_; }
    const char* invalid_reason() const { return invalid_reason_; }
    bool warming() const { return warmup_left_ != 0; }
    bool end_warmup_frame() { return warmup_left_ != 0 && --warmup_left_ == 0; }
    void skipped()
    {
        if (benchmark_.empty() || warming()) return;
        ++skipped_frames_;
        invalidate("a frame was skipped while measuring");
    }
    std::size_t skipped_frames() const { return skipped_frames_; }
    void swapchain_rebuilt()
    {
        if (benchmark_.empty() || warming()) return;
        invalidate("the swapchain was rebuilt while measuring");
    }
    const Scenario& script() const { return scenario_; }
    void set_vertical_fov(float radians) { vertical_fov_ = radians; }
    void submit_due(Sandbox& sandbox)
    {
        while (next_ < scenario_.events.size() && scenario_.events[next_].tick <= sandbox.clock.tick())
        {
            sandbox.effects.submit(scenario_.events[next_++].event);
        }
    }
    void record(double cpu_ms, std::optional<GpuTimings> gpu, const Sandbox& sandbox)
    {
        cpu_ms_.push_back(cpu_ms);
        if (gpu && gpu->sample_id != last_gpu_sample_)
        {
            last_gpu_sample_ = gpu->sample_id;
            draw_ms_.push_back(gpu->draw_ms);
            compute_ms_.push_back(gpu->compute_ms);
            for (std::uint32_t index = 0; index < gpu->region_count; ++index)
            {
                const auto& region = gpu->regions[index];
                auto found = std::find_if(regions_.begin(), regions_.end(), [&region](const RegionSum& sum) { return sum.label == region.label; });
                if (found == regions_.end()) found = regions_.insert(regions_.end(), {region.label, 0, 0});
                found->milliseconds += region.milliseconds;
                ++found->samples;
            }
        }
        peak_effects_ = std::max(peak_effects_, static_cast<std::uint32_t>(sandbox.effects.explosions()));
    }
    void count(const ParticleCounters& counters)
    {
        ++counter_samples_;
        peak_alive_ = std::max(peak_alive_, counters.alive);
        alive_sum_ += counters.alive;
        checksum_ = counter_checksum(checksum_, counters);
    }
    int report(const Sandbox& sandbox, const ApplicationOptions& options, std::string_view gpu_name, int width, int height,
        bool overlay, const std::string& extra, const CounterLog::Summary* summary = nullptr, const std::string& tail = {});

private:
    Scenario scenario_;
    std::size_t next_ = 0;
    std::vector<double> cpu_ms_;
    std::vector<double> draw_ms_;
    std::vector<double> compute_ms_;
    std::uint64_t last_gpu_sample_ = 0;
    struct RegionSum
    {
        const char* label;
        double milliseconds;
        std::size_t samples;
    };
    std::vector<RegionSum> regions_;
    std::uint32_t peak_effects_ = 0;
    std::size_t counter_samples_ = 0;
    std::uint32_t peak_alive_ = 0;
    std::uint64_t alive_sum_ = 0;
    std::uint64_t checksum_ = checksum_basis;
    bool valid_ = true;
    const char* invalid_reason_ = nullptr;
    std::string benchmark_;
    std::uint32_t warmup_left_ = 0;
    std::size_t skipped_frames_ = 0;
    float vertical_fov_ = Camera{}.vertical_fov;
};

std::string benchmark_tail(const ScenarioRun& run, const CounterLog::Summary& summary, const GraphicsDevice& device,
    Renderer renderer, bool gpu_validation, std::optional<float> display_hz, bool focused,
    const std::optional<GpuMemory>& memory_start, const std::optional<GpuMemory>& memory_end);

class RunRecords final
{
public:
    RunRecords(bool realism, bool automatic) : realism_(realism), automatic_(automatic) {}
    void after_frame(const Sandbox& sandbox, RenderResult result);
    void add_counters(const ParticleCounters& counters);
    std::uint32_t mismatches() const { return twin_mismatches_; }
    std::string json(const Sandbox& sandbox) const;
    CoverageStatistics coverage;

private:
    bool realism_;
    bool automatic_;
    std::uint32_t peak_clusters_ = 0;
    std::uint32_t peak_residual_ = 0;
    std::uint32_t ghosts_ = 0;
    std::deque<std::pair<std::uint32_t, std::uint32_t>> rotor_expected_;
    std::uint32_t twin_mismatches_ = 0;
    std::uint32_t peak_dust_ = 0;
    std::int32_t lowest_hub_cm_ = std::numeric_limits<std::int32_t>::max();
    std::pair<std::uint32_t, std::uint32_t> rotor_slot_{never, never};
    std::uint32_t peak_knots_ = 0;
    std::pair<std::uint32_t, std::uint32_t> jet_slot_{never, never};
    std::uint32_t peak_jet_dust_ = 0;
};
}
