#pragma once

#include "../simulation/explosion_system.hpp"

#include <cstdint>

namespace pyroclast
{
enum class SmokeResolution { full, half, automatic };

constexpr const char* smoke_resolution_name(SmokeResolution resolution)
{
    return resolution == SmokeResolution::half ? "half" : resolution == SmokeResolution::automatic ? "auto" : "full";
}

enum class EffectOrder { origin, none, cloud };

constexpr const char* effect_order_name(EffectOrder order)
{
    return order == EffectOrder::none ? "none" : order == EffectOrder::cloud ? "cloud" : "origin";
}

struct SmokeBudget
{
    bool count = false;
    double limit = 0;
};

struct ScalabilitySettings
{
    bool coverage = false;
    SmokeResolution smoke_resolution = SmokeResolution::full;
    SmokeBudget smoke_budget;
    bool half_light = false;
    float smoke_lod = 0;
    float faint_floor = 0;
    EffectOrder effect_order = EffectOrder::origin;
    SlotPolicy slot_policy = SlotPolicy::drop_new;
};

constexpr const char* slot_policy_name(SlotPolicy policy)
{
    return policy == SlotPolicy::evict_oldest ? "evict-oldest" : policy == SlotPolicy::graduate_oldest ? "graduate-oldest" : "drop-new";
}
}
