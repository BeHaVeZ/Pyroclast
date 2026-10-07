#include "core/math.hpp"

#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;

void check(bool value, const char* name)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

bool approx(float a, float b, float tolerance = 1e-4f)
{
    return std::abs(a - b) <= tolerance;
}

pyroclast::float3 project(const pyroclast::float4x4& clip_from_world, pyroclast::float3 point)
{
    const auto clip = clip_from_world * pyroclast::float4{point.x, point.y, point.z, 1};
    return {clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
}
}

int main()
{
    using namespace pyroclast;
    constexpr float pi = 3.14159265358979f;

    const auto view = look_to({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
    const auto projection = perspective_reverse_z(pi / 2, 2.0f, 0.5f);
    const auto clip_from_world = projection * view;
    const auto ahead = project(clip_from_world, {0, 0, -0.5f});
    check(approx(ahead.x, 0) && approx(ahead.y, 0) && approx(ahead.z, 1), "the near plane maps to depth 1");
    check(approx(project(clip_from_world, {0, 0, -10}).z, 0.05f), "depth is near / distance (reverse Z)");
    check(project(clip_from_world, {0, 0, -1e6f}).z > 0, "depth stays positive towards infinity");
    check(approx(project(clip_from_world, {2, 0, -1}).x, 1), "x reaches the edge at the horizontal field of view");
    check(approx(project(clip_from_world, {0, 1, -1}).y, 1) && project(clip_from_world, {0, 1, -2}).y > 0,
        "y points up in clip space");

    const auto turned = look_to({10, 2, 0}, {-1, 0, 0}, {0, 1, 0});
    const auto origin_in_view = turned * float4{0, 2, 0, 1};
    check(approx(origin_in_view.x, 0) && approx(origin_in_view.y, 0) && approx(origin_in_view.z, -10), "look_to puts the target on -Z");
    const auto side = turned * float4{0, 2, -5, 1};
    check(side.x > 0, "world -Z is to the camera's right when it looks along -X");

    const auto round_trip = inverse(clip_from_world) * clip_from_world;
    bool identity = true;
    for (int column = 0; column < 4; ++column)
    {
        const auto expected = float4x4::identity().columns[column];
        const auto actual = round_trip.columns[column];
        identity = identity && approx(actual.x, expected.x) && approx(actual.y, expected.y) && approx(actual.z, expected.z) && approx(actual.w, expected.w);
    }
    check(identity, "inverse(M) * M is the identity");
    const auto world = inverse(clip_from_world) * float4{0, 0, 0.05f, 1};
    check(approx(world.z / world.w, -10, 1e-2f), "unprojecting a depth recovers the distance");

    const Ray down{{0, 10, 0}, {0, -1, 0}};
    const auto hit = intersect_plane(down, {0, 1, 0}, 2);
    check(hit && approx(*hit, 8), "a ray hits the plane below it");
    check(!intersect_plane({{0, 10, 0}, {0, 1, 0}}, {0, 1, 0}, 2), "a ray pointing away misses");
    check(!intersect_plane({{0, 10, 0}, {1, 0, 0}}, {0, 1, 0}, 2), "a parallel ray misses");

    const Box box{{-1, 0, -1}, {1, 2, 1}};
    const auto entered = intersect_box({{-5, 1, 0}, {1, 0, 0}}, box);
    check(entered && approx(*entered, 4), "a ray enters the box at its near face");
    const auto inside = intersect_box({{0, 1, 0}, {0, 0, 1}}, box);
    check(inside && approx(*inside, 0), "a ray starting inside hits at distance 0");
    check(!intersect_box({{-5, 3, 0}, {1, 0, 0}}, box), "a parallel ray above the box misses");
    check(!intersect_box({{5, 1, 0}, {1, 0, 0}}, box), "a box behind the ray is missed");
    const float diagonal = 1 / std::sqrt(3.0f);
    const auto corner = intersect_box({{-3, -2, -3}, {diagonal, diagonal, diagonal}}, box);
    check(corner && approx(*corner, std::sqrt(12.0f)), "a diagonal ray enters at the box corner");

    check(approx(length(cross(float3{1, 0, 0}, float3{0, 1, 0}) - float3{0, 0, 1}), 0), "cross follows the right-hand rule");
    return failures == 0 ? 0 : 1;
}
