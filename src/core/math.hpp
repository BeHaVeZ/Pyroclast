#pragma once

#include <array>
#include <cmath>
#include <optional>

namespace pyroclast
{
struct float2
{
    float x = 0;
    float y = 0;
};

struct float3
{
    float x = 0;
    float y = 0;
    float z = 0;
};

struct float4
{
    float x = 0;
    float y = 0;
    float z = 0;
    float w = 0;
};

constexpr float3 operator+(float3 a, float3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr float3 operator-(float3 a, float3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr float3 operator-(float3 a) { return {-a.x, -a.y, -a.z}; }
constexpr float3 operator*(float3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
constexpr float3 operator*(float s, float3 a) { return a * s; }
constexpr float3 operator/(float3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }
constexpr float3& operator+=(float3& a, float3 b) { return a = a + b; }
constexpr float dot(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr float3 cross(float3 a, float3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(float3 a) { return std::sqrt(dot(a, a)); }
inline float3 normalize(float3 a) { return a / length(a); }
constexpr float4 operator*(float4 a, float s) { return {a.x * s, a.y * s, a.z * s, a.w * s}; }
constexpr float4 operator+(float4 a, float4 b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }

struct float4x4
{
    std::array<float4, 4> columns{};

    static constexpr float4x4 identity()
    {
        return {{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}}};
    }
};

constexpr float4 operator*(const float4x4& m, float4 v)
{
    return m.columns[0] * v.x + m.columns[1] * v.y + m.columns[2] * v.z + m.columns[3] * v.w;
}

constexpr float4x4 operator*(const float4x4& a, const float4x4& b)
{
    return {{{a * b.columns[0], a * b.columns[1], a * b.columns[2], a * b.columns[3]}}};
}

float4x4 look_to(float3 eye, float3 forward, float3 up);
float4x4 perspective_reverse_z(float vertical_fov_radians, float aspect_ratio, float near_plane);
float4x4 inverse(const float4x4& m);

struct Ray
{
    float3 origin;
    float3 direction;
};

std::optional<float> intersect_plane(const Ray& ray, float3 normal, float offset);

struct Box
{
    float3 minimum;
    float3 maximum;
};

std::optional<float> intersect_box(const Ray& ray, const Box& box);
}
