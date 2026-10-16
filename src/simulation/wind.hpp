#pragma once

#include "../core/math.hpp"

#include <cstdint>
#include <optional>

namespace pyroclast
{
// The wind 15 m above the ground: speed in m/s, direction in degrees from +x towards +z, gustiness 0-1.
struct WindSettings
{
    float speed = 3;
    float direction_degrees = 0;
    float gustiness = 0.3f;
};

float3 wind_velocity(const WindSettings& settings, std::uint32_t tick, bool gusts);

inline constexpr float maximum_wind_speed = 40;
// The command line and the overlay accept wind only through this.
std::optional<WindSettings> validated(WindSettings settings);

// Must match lingering_wind in shaders/realism.hlsli.
struct WindProfile
{
    float strength = 1;
    float veer = 0;
};
WindProfile wind_at_height(float height);

// The wind's integral since tick 0 (metres), advanced only by committed ticks, so a skipped frame does not count twice.
class WindDrift final
{
public:
    float3 at(std::uint32_t tick, const WindSettings& settings, bool gusts) const;
    void advance(std::uint32_t tick, const WindSettings& settings, bool gusts);
    std::uint32_t tick() const { return tick_; }

private:
    double x_ = 0;
    double y_ = 0;
    double z_ = 0;
    std::uint32_t tick_ = 0;
};
}
