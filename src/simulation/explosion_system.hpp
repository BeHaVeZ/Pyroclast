#pragma once

#include "explosion_presets.hpp"
#include "../core/math.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace pyroclast
{
struct ExplosionEvent
{
    float3 position;
    float3 normal{0, 1, 0};
    std::uint32_t seed = 0;
    ExplosionPreset preset = ExplosionPreset::small_he;
    SurfaceType surface = SurfaceType::dirt;
};

struct EffectSlot
{
    float3 origin;
    float3 normal{0, 1, 0};
    std::uint32_t seed = 0;
    std::uint32_t generation = 0;
    std::uint32_t start_tick = 0;
    ExplosionPreset preset = ExplosionPreset::small_he;
    std::uint32_t effect_class = 0;
    std::uint32_t lifetime_ticks = 0;
    bool active = false;
};

enum class SlotPolicy { drop_new, evict_oldest };

class ExplosionSystem final
{
public:
    explicit ExplosionSystem(std::uint32_t slots);
    std::uint32_t capacity() const { return static_cast<std::uint32_t>(slots_.size()); }
    void submit(const ExplosionEvent& event);
    void clear();
    void start(std::uint32_t tick, SlotPolicy policy = SlotPolicy::drop_new);
    std::span<const EffectSlot> slots() const { return slots_; }
    std::span<const std::uint32_t> active() const { return active_; }
    std::uint64_t started() const { return started_; }
    std::uint64_t dropped() const { return dropped_; }
    std::uint64_t evicted() const { return evicted_; }
    std::size_t pending() const { return pending_.size(); }

private:
    std::vector<EffectSlot> slots_;
    std::vector<std::uint32_t> active_;
    std::vector<ExplosionEvent> pending_;
    std::uint64_t started_ = 0;
    std::uint64_t dropped_ = 0;
    std::uint64_t evicted_ = 0;
};
}
