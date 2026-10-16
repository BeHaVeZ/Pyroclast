#pragma once

#include "explosion_presets.hpp"
#include "simulation_clock.hpp"
#include "../core/math.hpp"
#include "../graphics/realism_layout.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

namespace pyroclast
{
// Written to FrameConstants::effect_residual for the effect's slot.
struct ResidualLink
{
    std::uint32_t ring_group = 0;
    std::uint32_t cluster = 0;
    std::uint32_t tag = 0;
    std::uint32_t graduation_tick = 0;
    std::uint32_t longest_ticks = 0;
    std::uint64_t serial = 0;
};

// A forced fade ends at fade_end (no_fade: none) and lasts (fade_code + 1) * fade_unit_ticks.
struct ResidualCluster
{
    std::uint32_t slot = 0;
    std::uint32_t ring_group = 0;
    std::uint32_t groups = 0;
    std::uint32_t records = 0;
    std::uint32_t tag = 0;
    std::uint32_t start_tick = 0;
    std::uint32_t graduation_tick = 0;
    std::uint32_t natural_end = 0;
    std::uint32_t longest_ticks = 0;
    std::uint32_t variation = realism_layout::scale_one;
    std::uint32_t fade_end = realism_layout::no_fade;
    std::uint32_t fade_code = 0;
    float3 origin;
    float3 drift_start;
    ExplosionPreset preset = ExplosionPreset::small_he;
    std::uint64_t serial = 0;

    std::uint32_t visible_end() const { return natural_end < fade_end ? natural_end : fade_end; }
    bool listed(std::uint32_t tick) const { return graduation_tick <= tick && tick < visible_end(); }
};

struct LingerSettings
{
    std::uint32_t linger_ticks = 75 * ticks_per_second;
    bool variation = false;
};

// Every decision is an integer function of the tick made when an effect starts, so the same events give the same
// clusters however ticks fall into frames.
class ResidualPool final
{
public:
    // Nothing when the ring head would reach a reservation still waiting to graduate (counted by refused()).
    // `variation` is the effect's life factor in 1/1024.
    std::optional<ResidualLink> reserve(std::uint32_t tick, const RealismParameters& realism, const LingerSettings& settings, float3 origin,
        float3 drift_start, ExplosionPreset preset, std::uint64_t serial, std::uint32_t variation = realism_layout::scale_one);
    std::optional<std::uint32_t> earliest_move(const ResidualLink& link, std::uint32_t tick) const;
    std::optional<ResidualLink> graduate_early(const ResidualLink& link, std::uint32_t tick, std::uint32_t graduation_tick);
    void clear();
    void retire(std::uint32_t tick);
    std::span<const ResidualCluster> clusters() const { return clusters_; }
    const ResidualCluster* find(const ResidualLink& link) const;
    // Since startup; last_scale() is in 1/1024.
    std::uint64_t forced_fades() const { return forced_fades_; }
    std::uint32_t last_scale() const { return last_scale_; }
    // Must stay 0.
    std::uint64_t refused() const { return refused_; }

    // Must match residual_death_tick in shaders/realism.hlsli.
    static std::uint32_t death_tick(std::uint32_t start_tick, std::uint32_t graduation_tick, std::uint32_t longest_ticks,
        std::uint32_t variation, std::uint32_t jitter);

private:
    void force_fade(ResidualCluster& cluster, std::uint32_t tick, std::uint32_t end);
    std::vector<ResidualCluster> clusters_;
    std::uint32_t head_ = 0;
    std::uint32_t next_tag_ = 0;
    std::deque<std::pair<std::uint32_t, std::uint32_t>> recent_;
    std::uint64_t forced_fades_ = 0;
    std::uint64_t refused_ = 0;
    std::uint32_t last_scale_ = realism_layout::scale_one;
};
}
