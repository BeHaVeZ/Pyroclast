#include "camera.hpp"

#include <algorithm>
#include <cmath>

namespace pyroclast
{
namespace
{
constexpr float pitch_limit = 1.55f;
constexpr float look_sensitivity = 0.0025f;
}

float3 Camera::forward() const
{
    return {std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)};
}

float3 Camera::right() const
{
    return {std::cos(yaw), 0, std::sin(yaw)};
}

void Camera::look_at(float3 target)
{
    const float3 direction = normalize(target - position);
    yaw = std::atan2(direction.x, -direction.z);
    pitch = std::clamp(std::asin(std::clamp(direction.y, -1.0f, 1.0f)), -pitch_limit, pitch_limit);
}

void fly(Camera& camera, const CameraInput& input, float seconds, float speed)
{
    camera.yaw += input.look.x * look_sensitivity;
    camera.pitch = std::clamp(camera.pitch - input.look.y * look_sensitivity, -pitch_limit, pitch_limit);
    const float distance = speed * (input.fast ? 4.0f : 1.0f) * seconds;
    camera.position += (camera.right() * input.move.x + float3{0, 1, 0} * input.move.y + camera.forward() * input.move.z) * distance;
}
}
