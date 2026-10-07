#pragma once

#include "../core/math.hpp"

#include <cstdint>
#include <optional>

namespace pyroclast
{
struct WindSettings
{
    float speed = 3;
    float direction_degrees = 0;
    float gustiness = 0.3f;
};

float3 wind_velocity(const WindSettings& settings, std::uint32_t tick, bool gusts);

inline constexpr float maximum_wind_speed = 40;
std::optional<WindSettings> validated(WindSettings settings);
}
