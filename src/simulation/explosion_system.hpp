#pragma once

#include "explosion_presets.hpp"
#include "residual_pool.hpp"
#include "../core/math.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
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

enum class EffectKind { explosion, rotor, rotor_disc, jet };

struct EffectSlot
{
    EffectKind kind = EffectKind::explosion;
    float3 origin;
    float3 normal{0, 1, 0};
    std::uint32_t seed = 0;
    std::uint32_t generation = 0;
    std::uint32_t start_tick = 0;
    ExplosionPreset preset = ExplosionPreset::small_he;
    std::uint32_t effect_class = 0;
    std::uint32_t lifetime_ticks = 0;
    bool active = false;
    std::uint64_t serial = 0;
    std::uint32_t latched = 0;
    float3 drift_start;
    std::optional<ResidualLink> residual;
    bool graduating_early = false;
};

enum class SlotPolicy { drop_new, evict_oldest, graduate_oldest };

struct RealismStart
{
    std::uint32_t mask = 0;
    LingerSettings linger;
    float3 drift;
};

class ExplosionSystem final
{
public:
    explicit ExplosionSystem(std::uint32_t slots);
    std::uint32_t capacity() const { return static_cast<std::uint32_t>(slots_.size()); }
    void submit(const ExplosionEvent& event);
    void clear();
    void start(std::uint32_t tick, SlotPolicy policy = SlotPolicy::drop_new, const RealismStart* realism = nullptr);
    std::span<const EffectSlot> slots() const { return slots_; }
    std::span<const std::uint32_t> active() const { return active_; }
    std::uint64_t started() const { return started_; }
    std::uint64_t dropped() const { return dropped_; }
    std::uint64_t evicted() const { return evicted_; }
    std::uint64_t deferred() const { return deferred_; }
    std::size_t explosions() const { return active_.size() - (rotor_slot() ? 1 : 0) - (rotor_disc_slot() ? 1 : 0) - (jet_slot() ? 1 : 0); }
    void request_rotor(std::uint32_t seed, float3 origin, std::uint32_t end_tick);
    std::optional<std::uint32_t> rotor_slot() const;
    std::optional<std::uint32_t> rotor_disc_slot() const;
    bool rotor_requested() const { return rotor_request_.has_value(); }
    void request_jet(std::uint32_t seed, float3 origin, std::uint32_t end_tick);
    std::optional<std::uint32_t> jet_slot() const;
    bool jet_requested() const { return jet_request_.has_value(); }
    std::size_t waiting() const { return waiting_.size(); }
    std::size_t pending() const { return pending_.size() + waiting_.size(); }
    const ResidualPool& residual() const { return residual_; }

private:
    std::vector<EffectSlot> slots_;
    ResidualPool residual_;
    std::vector<RealismParameters> realism_;
    std::vector<std::uint32_t> realism_ticks_;
    std::vector<std::uint32_t> early_ticks_;
    std::deque<ExplosionEvent> waiting_;
    std::uint64_t deferred_ = 0;
    bool graduate_one_early(std::uint32_t tick);
    std::vector<std::uint32_t> active_;
    std::vector<ExplosionEvent> pending_;
    struct RotorRequest
    {
        std::uint32_t seed = 0;
        float3 origin;
        std::uint32_t end_tick = 0;
    };
    std::optional<RotorRequest> rotor_request_;
    std::optional<RotorRequest> jet_request_;
    std::uint64_t started_ = 0;
    std::uint64_t dropped_ = 0;
    std::uint64_t evicted_ = 0;
};
}
