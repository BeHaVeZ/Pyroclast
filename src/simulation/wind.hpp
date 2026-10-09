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

struct WindProfile
{
    float strength = 1;
    float veer = 0;
};
WindProfile wind_at_height(float height);

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
