#pragma once

#include <cstdint>

namespace pyroclast
{
enum class SmokeResolution { full, half, automatic };

constexpr const char* smoke_resolution_name(SmokeResolution resolution)
{
    return resolution == SmokeResolution::half ? "half" : resolution == SmokeResolution::automatic ? "auto" : "full";
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
};
}
