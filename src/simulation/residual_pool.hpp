#pragma once

#include "explosion_presets.hpp"
#include "../core/math.hpp"
#include "../graphics/realism_layout.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

namespace pyroclast
{
struct ResidualLink
{
    std::uint32_t ring_group = 0;
    std::uint32_t cluster = 0;
    std::uint32_t tag = 0;
    std::uint32_t graduation_tick = 0;
    std::uint32_t longest_ticks = 0;
    std::uint64_t serial = 0;
};

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
    std::uint32_t linger_ticks = 75 * 60;
    bool variation = false;
};

class ResidualPool final
{
public:
    std::optional<ResidualLink> reserve(std::uint32_t tick, const RealismParameters& realism, const LingerSettings& settings, float3 origin,
        float3 drift_start, ExplosionPreset preset, std::uint64_t serial, std::uint32_t variation = realism_layout::scale_one);
    std::optional<std::uint32_t> earliest_move(const ResidualLink& link, std::uint32_t tick) const;
    std::optional<ResidualLink> graduate_early(const ResidualLink& link, std::uint32_t tick, std::uint32_t graduation_tick);
    void clear();
    void retire(std::uint32_t tick);
    std::span<const ResidualCluster> clusters() const { return clusters_; }
    const ResidualCluster* find(const ResidualLink& link) const;
    std::uint64_t forced_fades() const { return forced_fades_; }
    std::uint32_t last_scale() const { return last_scale_; }
    std::uint64_t refused() const { return refused_; }

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
