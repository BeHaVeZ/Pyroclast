#pragma once

#include "../core/math.hpp"

namespace pyroclast
{
struct Camera
{
    float3 position{0, 10, 40};
    float yaw = 0;
    float pitch = 0;
    float vertical_fov = 1.0471976f;
    float near_plane = 0.1f;

    float3 forward() const;
    float3 right() const;
    float3 up() const { return cross(right(), forward()); }
    float4x4 view() const { return look_to(position, forward(), {0, 1, 0}); }
    float4x4 projection(float aspect_ratio) const { return perspective_reverse_z(vertical_fov, aspect_ratio, near_plane); }
    void look_at(float3 target);
};

struct CameraInput
{
    float3 move;
    bool fast = false;
    float2 look;
};

void fly(Camera& camera, const CameraInput& input, float seconds, float speed);
}
