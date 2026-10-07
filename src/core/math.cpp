#include "math.hpp"

#include <algorithm>
#include <cassert>
#include <limits>

namespace pyroclast
{
float4x4 look_to(float3 eye, float3 forward, float3 up)
{
    const float3 right = normalize(cross(forward, up));
    const float3 true_up = cross(right, forward);
    return {{{
        {right.x, true_up.x, -forward.x, 0},
        {right.y, true_up.y, -forward.y, 0},
        {right.z, true_up.z, -forward.z, 0},
        {-dot(right, eye), -dot(true_up, eye), dot(forward, eye), 1}}}};
}

float4x4 perspective_reverse_z(float vertical_fov_radians, float aspect_ratio, float near_plane)
{
    assert(vertical_fov_radians > 0 && aspect_ratio > 0 && near_plane > 0);
    const float focal = 1.0f / std::tan(vertical_fov_radians * 0.5f);
    return {{{
        {focal / aspect_ratio, 0, 0, 0},
        {0, focal, 0, 0},
        {0, 0, 0, -1},
        {0, 0, near_plane, 0}}}};
}

float4x4 inverse(const float4x4& m)
{
    float a[16];
    for (int column = 0; column < 4; ++column)
    {
        a[0 * 4 + column] = m.columns[column].x;
        a[1 * 4 + column] = m.columns[column].y;
        a[2 * 4 + column] = m.columns[column].z;
        a[3 * 4 + column] = m.columns[column].w;
    }
    float r[16];
    r[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    r[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    r[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    r[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    r[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    r[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    r[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    r[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    r[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    r[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    r[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    r[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    r[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    r[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    r[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    r[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    const float determinant = a[0] * r[0] + a[1] * r[4] + a[2] * r[8] + a[3] * r[12];
    assert(determinant != 0);
    float4x4 result;
    for (int column = 0; column < 4; ++column)
    {
        result.columns[column] = float4{r[0 * 4 + column], r[1 * 4 + column], r[2 * 4 + column], r[3 * 4 + column]} * (1.0f / determinant);
    }
    return result;
}

std::optional<float> intersect_plane(const Ray& ray, float3 normal, float offset)
{
    const float facing = dot(normal, ray.direction);
    if (std::abs(facing) < 1e-6f) return std::nullopt;
    const float distance = (offset - dot(normal, ray.origin)) / facing;
    return distance >= 0 ? std::optional<float>(distance) : std::nullopt;
}

std::optional<float> intersect_box(const Ray& ray, const Box& box)
{
    float entry = 0;
    float exit = std::numeric_limits<float>::max();
    const float origin[] = {ray.origin.x, ray.origin.y, ray.origin.z};
    const float direction[] = {ray.direction.x, ray.direction.y, ray.direction.z};
    const float minimum[] = {box.minimum.x, box.minimum.y, box.minimum.z};
    const float maximum[] = {box.maximum.x, box.maximum.y, box.maximum.z};
    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::abs(direction[axis]) < 1e-12f)
        {
            if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis]) return std::nullopt;
            continue;
        }
        float near_side = (minimum[axis] - origin[axis]) / direction[axis];
        float far_side = (maximum[axis] - origin[axis]) / direction[axis];
        if (near_side > far_side) std::swap(near_side, far_side);
        entry = std::max(entry, near_side);
        exit = std::min(exit, far_side);
        if (entry > exit) return std::nullopt;
    }
    return entry;
}
}
