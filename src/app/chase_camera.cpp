#include "chase_camera.hpp"

#include <algorithm>
#include <cmath>

namespace pyroclast
{
namespace
{
// Kept in step with camera.cpp's look sensitivity and pitch limit.
constexpr float look_sensitivity = 0.0025f;
constexpr float pitch_limit = 1.55f;
}

float view_heading(const Camera& view)
{
    return std::atan2(-std::cos(view.yaw), std::sin(view.yaw));
}

void ChaseCamera::follow_helicopter(const Camera& view)
{
    yaw_ = view.yaw;
    pitch_ = -0.3f;
    distance_ = 30;
    attached_ = true;
}

void ChaseCamera::follow_jet()
{
    yaw_ = 0;
    pitch_ = -0.12f;
    distance_ = 45;
    attached_ = true;
}

void ChaseCamera::orbit(float2 look)
{
    yaw_ += look.x * look_sensitivity;
    pitch_ = std::clamp(pitch_ - look.y * look_sensitivity, -pitch_limit, pitch_limit);
}

void ChaseCamera::zoom(float notches)
{
    distance_ = std::clamp(distance_ * std::pow(1.15f, -notches), closest, farthest);
}

float ChaseCamera::heading() const
{
    Camera view;
    view.yaw = yaw_;
    return view_heading(view);
}

Camera ChaseCamera::place(const Camera& view, float3 target, std::optional<float3> track, const Terrain& terrain) const
{
    Camera camera = view;
    camera.yaw = yaw_ + (track && (track->x != 0 || track->z != 0) ? std::atan2(track->x, -track->z) : 0.0f);
    camera.pitch = pitch_;
    camera.position = target - camera.forward() * distance_;
    const float lowest = terrain.height(camera.position.x, camera.position.z) + 2;
    if (camera.position.y < lowest)
    {
        camera.position.y = lowest;
        camera.look_at(target);
    }
    return camera;
}
}
