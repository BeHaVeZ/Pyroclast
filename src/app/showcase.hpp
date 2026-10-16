#pragma once

#include "camera.hpp"
#include "showcase_marks.hpp"
#include "../simulation/impact_patterns.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace pyroclast
{
class Showcase final
{
public:
    // Above every scripted seed.
    static constexpr std::uint32_t first_seed = 100000;
    static constexpr std::array<std::uint32_t, 4> counts{1, 10, 50, 100};
    // Intervals in ticks, radii in metres.
    static constexpr std::array<std::uint32_t, 9> bombardment_intervals{60, 30, 15, 10, 6, 4, 3, 2, 1};
    static constexpr float bombardment_radius_min = 5;
    static constexpr float bombardment_radius_max = 60;

    ExplosionPreset preset = ExplosionPreset::small_he;
    std::uint32_t count = 1;
    std::uint32_t next_seed = first_seed;
    std::uint32_t bombardment_interval = 15;
    float bombardment_radius = 30;
    std::optional<ImpactSequence> barrage;
    std::optional<ImpactSequence> bombardment;

    struct Fired
    {
        std::uint32_t queued = 0;
        std::uint32_t off_range = 0;
    };
    std::optional<Fired> fire(const Environment& environment, bool props, const Ray& ray, std::uint32_t events, std::string_view source,
        ExplosionSystem& effects);
    bool start_barrage(const Environment& environment, bool props, const Camera& camera, std::uint32_t tick);
    bool toggle_bombardment(const Environment& environment, bool props, const Camera& camera, std::uint32_t tick);
    void set_bombardment(std::uint32_t interval, float radius, std::uint32_t tick);
    std::uint32_t release_due(std::uint32_t tick, const Environment& environment, bool props, ExplosionSystem& effects);
    void stop();
    std::uint32_t next_count() const;
};

void showcase_marks(const DebugViews& views, const Showcase& showcase, const std::optional<SurfaceHit>& aim, const Camera& camera,
    const Environment& environment, bool props, std::vector<ShowcaseMark>& marks);

class SlotPressure final
{
public:
    static constexpr std::uint32_t recent_ticks = 60;

    struct Report
    {
        std::uint64_t events = 0;
        std::uint32_t first_tick = 0;
        std::uint32_t last_tick = 0;
    };
    std::optional<Report> record(std::uint32_t tick, double wall_seconds, std::uint32_t active, std::uint64_t dropped);
    std::optional<Report> clear(std::uint64_t dropped);
    std::uint64_t dropped_since_clear() const { return dropped_ - cleared_; }
    std::uint64_t dropped_recent(std::uint32_t tick) const;
    std::uint32_t peak_since_clear() const { return peak_; }

private:
    std::uint64_t dropped_ = 0;
    std::uint64_t cleared_ = 0;
    Report unreported_;
    std::uint32_t peak_ = 0;
    double last_report_ = -1e9;
    std::array<std::uint64_t, recent_ticks> buckets_{};
    std::array<std::uint32_t, recent_ticks> bucket_ticks_{};
};
}
