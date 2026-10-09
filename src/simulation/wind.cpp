#include "wind.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace pyroclast
{
float3 wind_velocity(const WindSettings& settings, std::uint32_t tick, bool gusts)
{
    constexpr float tau = 2 * std::numbers::pi_v<float>;
    const float seconds = static_cast<float>(tick) / 60.0f;
    float speed = settings.speed;
    float heading = std::remainder(settings.direction_degrees, 360.0f) * (std::numbers::pi_v<float> / 180.0f);
    if (gusts)
    {
        const float gust = 0.55f * std::sin(tau * seconds / 7.3f) + 0.3f * std::sin(tau * seconds / 2.9f + 1.7f) +
            0.15f * std::sin(tau * seconds / 1.3f + 0.4f);
        speed *= 1 + settings.gustiness * gust;
        heading += settings.gustiness * (12.0f * std::numbers::pi_v<float> / 180.0f) * std::sin(tau * seconds / 11.0f + 0.9f);
    }
    return {speed * std::cos(heading), 0, speed * std::sin(heading)};
}

WindProfile wind_at_height(float height)
{
    if (height <= 15) return {0.35f + 0.65f * std::clamp(height / 15, 0.0f, 1.0f), 0};
    return {std::pow(std::min(height, 200.0f) / 15, 0.2f), 0.34906585f * std::clamp((height - 15) / 200, 0.0f, 1.0f)};
}

float3 WindDrift::at(std::uint32_t tick, const WindSettings& settings, bool gusts) const
{
    double x = x_;
    double y = y_;
    double z = z_;
    for (std::uint32_t step = tick_; step < tick; ++step)
    {
        const auto wind = wind_velocity(settings, step, gusts);
        x += wind.x / 60.0;
        y += wind.y / 60.0;
        z += wind.z / 60.0;
    }
    return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
}

void WindDrift::advance(std::uint32_t tick, const WindSettings& settings, bool gusts)
{
    for (; tick_ < tick; ++tick_)
    {
        const auto wind = wind_velocity(settings, tick_, gusts);
        x_ += wind.x / 60.0;
        y_ += wind.y / 60.0;
        z_ += wind.z / 60.0;
    }
}

std::optional<WindSettings> validated(WindSettings settings)
{
    if (!std::isfinite(settings.speed) || !std::isfinite(settings.direction_degrees) || !std::isfinite(settings.gustiness) ||
        settings.speed < 0 || settings.speed > maximum_wind_speed || settings.gustiness < 0 || settings.gustiness > 1)
    {
        return std::nullopt;
    }
    settings.direction_degrees = std::remainder(settings.direction_degrees, 360.0f);
    return settings;
}
}
