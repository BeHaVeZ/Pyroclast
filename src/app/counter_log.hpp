#pragma once

#include "frame_statistics.hpp"
#include "../graphics/draw_validation.hpp"
#include "../graphics/graphics_device.hpp"
#include "../simulation/particle_system.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pyroclast
{
inline constexpr std::uint64_t checksum_basis = 0xcbf29ce484222325ull;
std::uint64_t counter_checksum(std::uint64_t checksum, const ParticleCounters& counters);

enum class FramePhase : std::uint8_t { warmup, measured };

class CounterLog final
{
public:
    explicit CounterLog(bool coverage = false, bool smoke_resolution = false, bool lod = false, bool evicted = false, bool benchmark = false,
        bool realism = false) :
        coverage_(coverage), smoke_resolution_(smoke_resolution), lod_(coverage && lod), evicted_(evicted), benchmark_(benchmark),
        realism_(realism)
    {
    }
    struct Frame
    {
        std::uint32_t tick = 0;
        std::uint32_t steps = 0;
        std::uint32_t active = 0;
        std::uint64_t started = 0;
        std::uint64_t dropped = 0;
        bool readback = false;
        bool half = false;
        std::uint64_t evicted = 0;
        FramePhase phase = FramePhase::measured;
        double begin_ms = 0;
        double submit_ms = 0;
        SubmitTimings device;
        FrameWork work;
        std::uint32_t clusters = 0;
        std::uint64_t forced = 0;
        std::uint32_t linger_scale = 0;
        std::uint64_t deferred = 0;
    };

    void add_frame(const Frame& frame);
    void set_cpu_ms(double milliseconds);
    void add_timing(const GpuTimings& timings);
    void add_counters(const ParticleCounters& counters);
    std::size_t mismatches() const { return mismatches_; }
    std::size_t size() const { return rows_.size(); }
    void reserve(std::size_t rows) { rows_.reserve(rows); }
    std::string csv() const;

    struct RegionSummary
    {
        std::string label;
        std::size_t samples = 0;
        FrameStatistics statistics{};
    };
    struct CountSummary
    {
        double mean = 0;
        std::uint32_t peak = 0;
    };
    struct Summary
    {
        std::size_t frames = 0;
        std::size_t gpu_samples = 0;
        std::size_t counter_records = 0;
        std::optional<FrameStatistics> cpu, frame, submit, wait, present, draw, compute, gpu_frame;
        std::vector<RegionSummary> regions;
        std::uint32_t peak_active = 0;
        std::uint32_t peak_alive = 0;
        double mean_alive = 0;
        std::uint64_t checksum = checksum_basis;
        FrameWork work_min;
        FrameWork work_max;
        std::optional<CountSummary> visible;
        std::optional<CountSummary> offscreen;
    };
    Summary summary() const;

private:
    struct Row
    {
        Frame frame;
        std::optional<ParticleCounters> counters;
        std::optional<GpuTimings> gpu;
        double cpu_ms = 0;
    };
    std::vector<std::string_view> region_labels() const;
    std::vector<Row> rows_;
    std::size_t next_timing_ = 0;
    std::size_t next_counters_ = 0;
    std::size_t mismatches_ = 0;
    bool coverage_ = false;
    bool smoke_resolution_ = false;
    bool lod_ = false;
    bool evicted_ = false;
    bool benchmark_ = false;
    bool realism_ = false;
};
}
