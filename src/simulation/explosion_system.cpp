#include "explosion_system.hpp"

#include <algorithm>
#include <cassert>

namespace pyroclast
{
ExplosionSystem::ExplosionSystem(std::uint32_t slots) : slots_(slots)
{
    assert(slots != 0);
    active_.reserve(slots);
}

void ExplosionSystem::submit(const ExplosionEvent& event)
{
    pending_.push_back(event);
}

void ExplosionSystem::clear()
{
    for (auto& slot : slots_) slot.active = false;
    active_.clear();
    pending_.clear();
}

void ExplosionSystem::start(std::uint32_t tick)
{
    std::erase_if(active_, [this, tick](std::uint32_t index) {
        auto& slot = slots_[index];
        if (tick - slot.start_tick < slot.lifetime_ticks) return false;
        slot.active = false;
        return true;
    });
    std::uint32_t candidate = 0;
    for (const auto& event : pending_)
    {
        while (candidate < slots_.size() && slots_[candidate].active) ++candidate;
        if (candidate == slots_.size())
        {
            ++dropped_;
            continue;
        }
        auto& slot = slots_[candidate];
        slot.origin = event.position;
        slot.normal = event.normal;
        slot.seed = event.seed;
        slot.effect_class = effect_class_index(event.preset, event.surface);
        slot.lifetime_ticks = effect_ticks(event.preset, event.surface);
        slot.start_tick = tick;
        slot.active = true;
        slot.generation = slot.generation == 0xffffffffu ? 1 : slot.generation + 1;
        active_.insert(std::lower_bound(active_.begin(), active_.end(), candidate), candidate);
        ++started_;
    }
    pending_.clear();
}
}
