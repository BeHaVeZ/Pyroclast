#include "explosion_system.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace pyroclast
{
ExplosionSystem::ExplosionSystem(std::uint32_t slots) : slots_(slots)
{
    assert(slots != 0);
    active_.reserve(slots);
    for (std::uint32_t preset = 0; preset < preset_count; ++preset)
    {
        for (std::uint32_t surface = 0; surface < surface_type_count; ++surface)
        {
            const auto kind = static_cast<ExplosionPreset>(preset);
            const auto ground = static_cast<SurfaceType>(surface);
            realism_.push_back(make_effect_class(kind, ground).realism);
            realism_ticks_.push_back(realism_effect_ticks(kind, ground));
            early_ticks_.push_back(realism_early_ticks(kind, ground));
        }
    }
}

void ExplosionSystem::submit(const ExplosionEvent& event)
{
    pending_.push_back(event);
}

void ExplosionSystem::clear()
{
    for (auto& slot : slots_)
    {
        slot.active = false;
        slot.residual.reset();
    }
    active_.clear();
    pending_.clear();
    waiting_.clear();
    residual_.clear();
    rotor_request_.reset();
    jet_request_.reset();
}

void ExplosionSystem::request_jet(std::uint32_t seed, float3 origin, std::uint32_t end_tick)
{
    if (const auto slot = jet_slot())
    {
        auto& jet = slots_[*slot];
        jet.lifetime_ticks = end_tick > jet.start_tick ? end_tick - jet.start_tick : 1;
        return;
    }
    jet_request_ = RotorRequest{seed, origin, end_tick};
}

std::optional<std::uint32_t> ExplosionSystem::jet_slot() const
{
    for (const auto index : active_)
    {
        if (slots_[index].kind == EffectKind::jet) return index;
    }
    return std::nullopt;
}

void ExplosionSystem::request_rotor(std::uint32_t seed, float3 origin, std::uint32_t end_tick)
{
    if (const auto slot = rotor_slot())
    {
        for (auto& rotor : slots_)
        {
            if (!rotor.active || rotor.kind == EffectKind::explosion) continue;
            rotor.lifetime_ticks = end_tick > rotor.start_tick ? end_tick - rotor.start_tick : 1;
        }
        return;
    }
    rotor_request_ = RotorRequest{seed, origin, end_tick};
}

std::optional<std::uint32_t> ExplosionSystem::rotor_slot() const
{
    for (const auto index : active_)
    {
        if (slots_[index].kind == EffectKind::rotor) return index;
    }
    return std::nullopt;
}

std::optional<std::uint32_t> ExplosionSystem::rotor_disc_slot() const
{
    for (const auto index : active_)
    {
        if (slots_[index].kind == EffectKind::rotor_disc) return index;
    }
    return std::nullopt;
}

bool ExplosionSystem::graduate_one_early(std::uint32_t tick)
{
    std::uint32_t chosen = 0;
    std::uint32_t chosen_move = 0;
    bool found = false;
    for (const auto index : active_)
    {
        const auto& slot = slots_[index];
        if (!slot.residual || slot.graduating_early) continue;
        const auto earliest = residual_.earliest_move(*slot.residual, tick);
        if (!earliest) continue;
        const std::uint32_t move = std::max(*earliest, slot.start_tick + early_ticks_[slot.effect_class]);
        if (move >= slot.residual->graduation_tick) continue;
        if (!found || move < chosen_move || (move == chosen_move && slot.start_tick < slots_[chosen].start_tick))
        {
            chosen = index;
            chosen_move = move;
            found = true;
        }
    }
    if (!found) return false;
    auto& slot = slots_[chosen];
    const auto moved = residual_.graduate_early(*slot.residual, tick, chosen_move);
    if (!moved) return false;
    slot.residual = *moved;
    slot.lifetime_ticks = chosen_move - slot.start_tick;
    slot.graduating_early = true;
    return true;
}

// A generation is never 0, which a zero-filled record holds, so every new effect respawns its slab.
void ExplosionSystem::start(std::uint32_t tick, SlotPolicy policy, const RealismStart* realism)
{
    residual_.retire(tick);
    std::erase_if(active_, [this, tick](std::uint32_t index) {
        auto& slot = slots_[index];
        if (tick - slot.start_tick < slot.lifetime_ticks) return false;
        slot.active = false;
        return true;
    });
    std::uint32_t candidate = 0;
    std::optional<std::uint32_t> held;
    if (rotor_request_)
    {
        assert(realism && !rotor_slot() && !rotor_disc_slot());
        std::array<std::uint32_t, 2> free{};
        std::uint32_t found = 0;
        for (std::uint32_t index = 0; index < slots_.size() && found < 2; ++index)
        {
            if (!slots_[index].active) free[found++] = index;
        }
        if (found == 2)
        {
            for (std::uint32_t part = 0; part < 2; ++part)
            {
                auto& slot = slots_[free[part]];
                slot = EffectSlot{part == 0 ? EffectKind::rotor : EffectKind::rotor_disc, rotor_request_->origin, {0, 1, 0}, rotor_request_->seed, slot.generation,
                    tick, ExplosionPreset::small_impact, rotor_class_index, rotor_request_->end_tick > tick ? rotor_request_->end_tick - tick : 1, true,
                    started_, 0, realism->drift, std::nullopt, false};
                slot.generation = slot.generation == 0xffffffffu ? 1 : slot.generation + 1;
                active_.insert(std::lower_bound(active_.begin(), active_.end(), free[part]), free[part]);
            }
            rotor_request_.reset();
        }
        else if (found == 1) held = free[0];
    }
    if (jet_request_)
    {
        assert(realism && !jet_slot());
        for (std::uint32_t index = 0; index < slots_.size(); ++index)
        {
            if (slots_[index].active || held == index) continue;
            auto& slot = slots_[index];
            slot = EffectSlot{EffectKind::jet, jet_request_->origin, {0, 1, 0}, jet_request_->seed, slot.generation, tick, ExplosionPreset::small_impact,
                jet_class_index, jet_request_->end_tick > tick ? jet_request_->end_tick - tick : 1, true, started_, 0, realism->drift, std::nullopt, false};
            slot.generation = slot.generation == 0xffffffffu ? 1 : slot.generation + 1;
            active_.insert(std::lower_bound(active_.begin(), active_.end(), index), index);
            jet_request_.reset();
            break;
        }
    }
    std::vector<ExplosionEvent> queue(waiting_.begin(), waiting_.end());
    const std::size_t promised = waiting_.size();
    waiting_.clear();
    queue.insert(queue.end(), pending_.begin(), pending_.end());
    for (std::size_t position = 0; position < queue.size(); ++position)
    {
        const auto& event = queue[position];
        while (candidate < slots_.size() && (slots_[candidate].active || held == candidate)) ++candidate;
        std::uint32_t chosen = candidate;
        bool evicting = false;
        if (candidate == slots_.size() && position < promised)
        {
            waiting_.push_back(event);
            continue;
        }
        if (candidate == slots_.size() && policy == SlotPolicy::graduate_oldest)
        {
            if (graduate_one_early(tick))
            {
                waiting_.push_back(event);
                ++deferred_;
                continue;
            }
            ++dropped_;
            continue;
        }
        if (candidate == slots_.size())
        {
            const auto oldest = std::min_element(active_.begin(), active_.end(), [this](std::uint32_t a, std::uint32_t b) {
                const bool rotor_a = slots_[a].kind != EffectKind::explosion;
                const bool rotor_b = slots_[b].kind != EffectKind::explosion;
                if (rotor_a != rotor_b) return rotor_b;
                return slots_[a].start_tick < slots_[b].start_tick || (slots_[a].start_tick == slots_[b].start_tick && a < b);
            });
            if (policy == SlotPolicy::drop_new || oldest == active_.end() || slots_[*oldest].start_tick == tick ||
                slots_[*oldest].kind != EffectKind::explosion)
            {
                ++dropped_;
                continue;
            }
            chosen = *oldest;
            evicting = true;
        }
        auto& slot = slots_[chosen];
        slot.kind = EffectKind::explosion;
        slot.origin = event.position;
        slot.normal = event.normal;
        slot.seed = event.seed;
        slot.preset = event.preset;
        slot.effect_class = effect_class_index(event.preset, event.surface);
        slot.lifetime_ticks = effect_ticks(event.preset, event.surface);
        slot.start_tick = tick;
        slot.active = true;
        slot.serial = started_;
        slot.latched = 0;
        slot.residual.reset();
        slot.graduating_early = false;
        if (realism)
        {
            slot.drift_start = realism->drift;
            const auto& lingering = realism_[slot.effect_class];
            if (((realism->mask >> realism_layout::linger_mask_bit) & 1) != 0 && lingering.residual_groups != 0)
            {
                const bool varied = ((realism->mask >> realism_layout::variation_mask_bit) & 1) != 0;
                const auto variation = varied ? event_variation(slot.seed).life : realism_layout::scale_one;
                auto linger = realism->linger;
                linger.variation = varied;
                slot.residual = residual_.reserve(tick, lingering, linger, slot.origin, slot.drift_start, slot.preset, slot.serial, variation);
                if (slot.residual)
                {
                    const bool skirt = ((realism->mask >> realism_layout::skirt_mask_bit) & 1) != 0;
                    slot.latched = realism_layout::latched_linger | (varied ? realism_layout::latched_variation : 0u) |
                        (skirt ? realism_layout::latched_skirt : 0u);
                    slot.lifetime_ticks = realism_ticks_[slot.effect_class];
                }
            }
        }
        slot.generation = slot.generation == 0xffffffffu ? 1 : slot.generation + 1;
        if (evicting) ++evicted_;
        else active_.insert(std::lower_bound(active_.begin(), active_.end(), chosen), chosen);
        ++started_;
    }
    pending_.clear();
}
}
