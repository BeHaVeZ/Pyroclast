#include "app/camera.hpp"
#include "simulation/terrain.hpp"

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

bool approx(float a, float b, float tolerance = 1e-3f)
{
    return std::abs(a - b) <= tolerance;
}
}

int main()
{
    using namespace pyroclast;
    const Terrain terrain;
    const auto heights = terrain.heights();
    check(heights.size() == static_cast<std::size_t>(Terrain::resolution) * Terrain::resolution, "one height per sample");
    check(approx(Terrain::half_extent, 256), "the terrain is 512 m across");

    check(approx(terrain.height(0, 0), 0) && approx(terrain.height(40, -55), 0), "the range is flat");
    const auto up = terrain.normal(10, 10);
    check(approx(up.x, 0) && approx(up.y, 1) && approx(up.z, 0), "the range faces straight up");
    float highest = 0;
    for (const float height : heights) highest = std::max(highest, height);
    check(highest > 5 && highest < 30, "hills of a few metres to a few tens of metres surround the range");

    const std::uint32_t column = 200;
    const std::uint32_t row = 30;
    const float x = column * Terrain::spacing - Terrain::half_extent;
    const float z = row * Terrain::spacing - Terrain::half_extent;
    const auto at = [&heights](std::uint32_t c, std::uint32_t r) { return heights[static_cast<std::size_t>(r) * Terrain::resolution + c]; };
    check(approx(terrain.height(x, z), at(column, row)), "a sample point returns its sample");
    check(approx(terrain.height(x + 0.5f * Terrain::spacing, z), 0.5f * (at(column, row) + at(column + 1, row))),
        "heights between samples are interpolated");
    check(approx(terrain.height(1e4f, 1e4f), at(Terrain::resolution - 1, Terrain::resolution - 1)), "outside the edge clamps");
    const auto slope = terrain.normal(x, z);
    check(approx(length(slope), 1) && slope.y > 0, "normals are unit length and face up");

    const auto down = terrain.raycast({{3, 50, -7}, {0, -1, 0}}, 200);
    check(down && approx(down->x, 3) && approx(down->y, 0, 0.01f) && approx(down->z, -7), "a ray straight down hits the range");
    const auto slanted_direction = normalize(float3{0, -1, -2});
    const auto slanted = terrain.raycast({{0, 20, 60}, slanted_direction}, 200);
    check(slanted && approx(slanted->y, 0, 0.01f) && approx(slanted->z, 20, 0.05f), "a slanted ray hits where it reaches the ground");
    check(!terrain.raycast({{0, 50, 0}, {0, 1, 0}}, 200), "a ray pointing up misses");
    check(!terrain.raycast({{0, 50, 0}, {0, -1, 0}}, 20), "a ray stops at its maximum distance");
    const auto hill_direction = normalize(float3{1, -0.05f, 0.3f});
    const auto hill = terrain.raycast({{0, 15, 0}, hill_direction}, 400);
    check(hill && approx(hill->y, terrain.height(hill->x, hill->z), 0.01f), "a hit lies on the surface");

    Camera camera;
    camera.position = {0, 0, 0};
    check(approx(camera.forward().z, -1) && approx(camera.right().x, 1) && approx(camera.up().y, 1), "default orientation");
    camera.look_at({10, 10, 0});
    const auto forward = camera.forward();
    check(approx(forward.x, std::sqrt(0.5f)) && approx(forward.y, std::sqrt(0.5f)), "look_at aims at the target");
    check(approx(dot(camera.right(), forward), 0) && approx(dot(camera.up(), forward), 0) && approx(camera.right().y, 0),
        "right and up are perpendicular to forward, right stays level");
    CameraInput input;
    input.move = {0, 0, 1};
    const auto before = camera.position;
    fly(camera, input, 0.5f, 10);
    check(approx(length(camera.position - before), 5) && approx(camera.position.x - before.x, 5 * forward.x),
        "flying forward moves speed times time along forward");
    input = {};
    input.look = {0, -100000};
    fly(camera, input, 0, 10);
    check(camera.pitch < 1.56f && camera.forward().y < 1, "pitch stops short of vertical");
    return failures == 0 ? 0 : 1;
}
