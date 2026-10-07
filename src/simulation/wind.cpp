#include "wind.hpp"

#include <cmath>
#include <numbers>

namespace pyroclast
{
float3 wind_velocity(const WindSettings& settings, std::uint32_t tick, bool gusts)
{
    constexpr float tau = 2 * std::numbers::pi_v<float>;
    const float seconds = static_cast<float>(tick) / 60.0f;
    float speed = settings.speed;
    float heading = settings.direction_degrees * std::numbers::pi_v<float> / 180.0f;
    if (gusts)
    {
        const float gust = 0.55f * std::sin(tau * seconds / 7.3f) + 0.3f * std::sin(tau * seconds / 2.9f + 1.7f) +
            0.15f * std::sin(tau * seconds / 1.3f + 0.4f);
        speed *= 1 + settings.gustiness * gust;
        heading += settings.gustiness * (12.0f * std::numbers::pi_v<float> / 180.0f) * std::sin(tau * seconds / 11.0f + 0.9f);
    }
    return {speed * std::cos(heading), 0, speed * std::sin(heading)};
}
}
