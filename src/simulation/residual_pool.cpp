#include "residual_pool.hpp"

#include <algorithm>
#include <array>
#include <cassert>

namespace pyroclast
{
namespace layout = realism_layout;

namespace
{
constexpr std::uint32_t largest_fade_code = 7;
constexpr std::uint32_t rate_window_ticks = 600;
// The per-event variation's largest factor, in 1/1024.
constexpr std::uint32_t largest_variation = 1229;

bool overlapping(const ResidualCluster& a, const ResidualCluster& b)
{
    return a.ring_group < b.ring_group + b.groups && b.ring_group < a.ring_group + a.groups;
}

bool in_the_way(const ResidualCluster& own, const ResidualCluster& other)
{
    return other.serial != own.serial && other.graduation_tick < own.graduation_tick && (overlapping(own, other) || other.slot == own.slot);
}
}

std::uint32_t ResidualPool::death_tick(std::uint32_t start_tick, std::uint32_t graduation_tick, std::uint32_t longest_ticks,
    std::uint32_t variation, std::uint32_t jitter)
{
    const std::uint32_t longest = (longest_ticks * variation) >> 10;
    const std::uint32_t graduated = graduation_tick - start_tick;
    const std::uint32_t after = longest > graduated ? longest - graduated : 0;
    const std::uint32_t life = after > jitter ? after - jitter : 0;
    return graduation_tick + std::max(layout::crossfade_ticks + 60, life);
}

void ResidualPool::force_fade(ResidualCluster& cluster, std::uint32_t tick, std::uint32_t end)
{
    if (cluster.visible_end() <= end) return;
    const std::uint32_t from = std::max(tick, cluster.graduation_tick);
    assert(end >= from + layout::fade_unit_ticks);
    const std::uint32_t available = end - from;
    const auto length = [](std::uint32_t code) { return (code + 1) * layout::fade_unit_ticks; };
    if (cluster.fade_end != layout::no_fade && cluster.graduation_tick <= tick && tick + length(cluster.fade_code) > cluster.fade_end)
    {
        const std::uint64_t left = cluster.fade_end - tick;
        const std::uint64_t old_length = length(cluster.fade_code);
        std::uint32_t code = largest_fade_code;
        while (code > 0 && length(code) * left / old_length > available) --code;
        cluster.fade_end = tick + static_cast<std::uint32_t>(length(code) * left / old_length);
        cluster.fade_code = code;
    }
    else
    {
        cluster.fade_code = std::min(largest_fade_code, available / layout::fade_unit_ticks - 1);
        cluster.fade_end = end;
    }
    ++forced_fades_;
}

std::optional<ResidualLink> ResidualPool::reserve(std::uint32_t tick, const RealismParameters& realism, const LingerSettings& settings,
    float3 origin, float3 drift_start, ExplosionPreset preset, std::uint64_t serial, std::uint32_t variation)
{
    const std::uint32_t groups = realism.residual_groups;
    assert(groups > 0 && groups <= layout::residual_groups && realism.graduation_ticks >= layout::fade_unit_ticks + 4);
    const std::uint32_t graduation = tick + realism.graduation_ticks + layout::crossfade_ticks;
    const std::uint32_t clear_by = graduation - 4;
    const std::uint32_t first = head_ + groups > layout::residual_groups ? 0 : head_;
    ResidualCluster cluster;
    cluster.ring_group = first;
    cluster.groups = groups;
    for (const auto& other : clusters_)
    {
        if (overlapping(cluster, other) && other.graduation_tick > tick)
        {
            ++refused_;
            return std::nullopt;
        }
    }
    head_ = first + groups;
    for (auto& other : clusters_)
    {
        if (overlapping(cluster, other)) force_fade(other, tick, clear_by);
    }
    std::array<std::uint32_t, layout::maximum_clusters> occupants{};
    for (const auto& other : clusters_) if (other.visible_end() > clear_by) ++occupants[other.slot];
    std::uint32_t slot = 0;
    while (slot < occupants.size() && occupants[slot] != 0) ++slot;
    if (slot == occupants.size())
    {
        ResidualCluster* oldest = nullptr;
        for (auto& other : clusters_)
        {
            if (other.visible_end() <= clear_by || other.graduation_tick > tick || occupants[other.slot] != 1) continue;
            if (!oldest || other.graduation_tick < oldest->graduation_tick) oldest = &other;
        }
        assert(oldest);
        force_fade(*oldest, tick, clear_by);
        slot = oldest->slot;
    }
    while (!recent_.empty() && recent_.front().first + rate_window_ticks <= tick) recent_.pop_front();
    recent_.push_back({tick, groups});
    std::uint64_t reserved = 0;
    for (const auto& [start, count] : recent_) reserved += count;
    const std::uint64_t events = recent_.size();
    const std::uint64_t longest = static_cast<std::uint64_t>(settings.linger_ticks) * (settings.variation ? largest_variation : 1024) / 1024;
    std::uint64_t scale = layout::scale_one;
    if (reserved * longest > std::uint64_t{layout::residual_groups} * rate_window_ticks)
    {
        scale = std::min(scale, std::uint64_t{layout::scale_one} * layout::residual_groups * rate_window_ticks / (reserved * longest));
    }
    if (events * longest > std::uint64_t{layout::maximum_clusters} * rate_window_ticks)
    {
        scale = std::min(scale, std::uint64_t{layout::scale_one} * layout::maximum_clusters * rate_window_ticks / (events * longest));
    }
    last_scale_ = static_cast<std::uint32_t>(std::max<std::uint64_t>(scale, 1));
    cluster.slot = slot;
    cluster.records = realism.residual_records;
    cluster.tag = next_tag_;
    next_tag_ = (next_tag_ + 1) & ((1u << layout::tag_bits) - 1);
    cluster.start_tick = tick;
    cluster.graduation_tick = graduation;
    cluster.longest_ticks = (settings.linger_ticks * last_scale_) >> 10;
    cluster.variation = variation;
    cluster.natural_end = death_tick(tick, graduation, cluster.longest_ticks, cluster.variation, 0);
    cluster.origin = origin;
    cluster.drift_start = drift_start;
    cluster.preset = preset;
    cluster.serial = serial;
    clusters_.push_back(cluster);
    return ResidualLink{first, slot, cluster.tag, graduation, cluster.longest_ticks, serial};
}

std::optional<std::uint32_t> ResidualPool::earliest_move(const ResidualLink& link, std::uint32_t tick) const
{
    const auto* own = find(link);
    if (!own) return std::nullopt;
    std::uint32_t move = tick + layout::crossfade_ticks + 4;
    for (const auto& other : clusters_)
    {
        if (!in_the_way(*own, other) || other.visible_end() + 4 <= move) continue;
        move = std::max(move, std::max(tick, other.graduation_tick) + layout::fade_unit_ticks + 4);
    }
    return move;
}

std::optional<ResidualLink> ResidualPool::graduate_early(const ResidualLink& link, std::uint32_t tick, std::uint32_t graduation_tick)
{
    auto found = std::find_if(clusters_.begin(), clusters_.end(), [&link](const ResidualCluster& cluster) { return cluster.serial == link.serial; });
    if (found == clusters_.end() || graduation_tick > found->graduation_tick || graduation_tick < tick + layout::crossfade_ticks + 4)
    {
        return std::nullopt;
    }
    const std::uint32_t clear_by = graduation_tick - 4;
    for (auto& other : clusters_)
    {
        if (in_the_way(*found, other)) force_fade(other, tick, clear_by);
    }
    found->graduation_tick = graduation_tick;
    found->natural_end = death_tick(found->start_tick, graduation_tick, found->longest_ticks, found->variation, 0);
    auto moved = link;
    moved.graduation_tick = graduation_tick;
    return moved;
}

void ResidualPool::clear()
{
    clusters_.clear();
    recent_.clear();
    head_ = 0;
}

void ResidualPool::retire(std::uint32_t tick)
{
    std::erase_if(clusters_, [tick](const ResidualCluster& cluster) { return cluster.visible_end() <= tick; });
}

const ResidualCluster* ResidualPool::find(const ResidualLink& link) const
{
    for (const auto& cluster : clusters_) if (cluster.serial == link.serial) return &cluster;
    return nullptr;
}
}
