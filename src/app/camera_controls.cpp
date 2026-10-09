#include "camera_controls.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pyroclast
{
namespace
{
float3 clamped_to_terrain(float3 feet)
{
    const float limit = Terrain::half_extent - walk_edge_margin;
    feet.x = std::clamp(feet.x, -limit, limit);
    feet.z = std::clamp(feet.z, -limit, limit);
    return feet;
}

constexpr float2 probe_offsets[] = {{0, 0}, {walk_radius, 0}, {-walk_radius, 0}, {0, walk_radius}, {0, -walk_radius}};

bool embedded(const Environment& environment, float3 feet)
{
    for (const float height : {walk_knee_height, walk_eye_height})
    {
        for (const auto& offset : probe_offsets)
        {
            if (environment.inside(feet + float3{offset.x, height, offset.y})) return true;
        }
    }
    return false;
}
}

Ray crosshair_ray(const Camera& camera)
{
    return camera.ray_through(0, 0, 1);
}

float walk_floor(const Environment& environment, bool props, float x, float z, float feet_y)
{
    const float terrain = environment.terrain().height(x, z);
    const float start = feet_y + walk_step_height;
    if (!props || start <= terrain) return terrain;
    const auto hit = environment.raycast({{x, start, z}, {0, -1, 0}}, start - terrain, true);
    return hit && hit->prop ? std::max(terrain, hit->point.y) : terrain;
}

float3 walk_move(const Environment& environment, bool props, float3 feet, float3 displacement)
{
    displacement.y = 0;
    const float distance = std::sqrt(displacement.x * displacement.x + displacement.z * displacement.z);
    if (distance == 0) return clamped_to_terrain(feet);
    const auto substeps = std::max(1, static_cast<int>(std::ceil(distance / walk_substep)));
    const float3 step = displacement / static_cast<float>(substeps);
    for (int index = 0; index < substeps; ++index)
    {
        const bool free = props && embedded(environment, feet);
        for (const float3 attempt : {step, float3{step.x, 0, 0}, float3{0, 0, step.z}})
        {
            const auto candidate = clamped_to_terrain(feet + attempt);
            if (!props || free || !embedded(environment, candidate))
            {
                feet = candidate;
                break;
            }
        }
    }
    return feet;
}

std::optional<float> walk_landing(const Environment& environment, bool props, float3 eye)
{
    const auto hit = environment.raycast({eye, {0, -1, 0}}, std::numeric_limits<float>::infinity(), props);
    if (!hit) return std::nullopt;
    return hit->point.y;
}

CameraRig::CameraRig(const Camera& start, const std::array<Camera, bookmark_count>& views,
    const std::array<std::string_view, bookmark_count>& names)
    : camera_(start)
{
    for (std::size_t index = 0; index < bookmark_count; ++index)
    {
        bookmarks_[index] = {views[index].position, views[index].yaw, views[index].pitch, views[index].vertical_fov, CameraMode::fly,
            names[index], false};
    }
}

bool CameraRig::update(const CameraInput& input, float seconds, const Environment& environment, bool props)
{
    const auto before = camera_;
    if (mode_ == CameraMode::fly)
    {
        if (input.move.x != 0 || input.move.y != 0 || input.move.z != 0 || input.look.x != 0 || input.look.y != 0)
        {
            fly(camera_, input, seconds, fly_speed_);
        }
    }
    else
    {
        CameraInput look{};
        look.look = input.look;
        fly(camera_, look, 0, 0);
        const float3 forward{std::sin(camera_.yaw), 0, -std::cos(camera_.yaw)};
        const float3 direction = camera_.right() * input.move.x + forward * input.move.z;
        const float length = std::sqrt(direction.x * direction.x + direction.z * direction.z);
        float3 displacement{};
        if (length > 0)
        {
            displacement = direction * (walk_speed_ * (input.fast ? 4.0f : 1.0f) * std::min(seconds, 0.1f) / length);
        }
        const auto feet = walk_move(environment, props, camera_.position - float3{0, walk_eye_height, 0}, displacement);
        camera_.position = {feet.x, walk_floor(environment, props, feet.x, feet.z, feet.y) + walk_eye_height, feet.z};
    }
    return camera_.position.x != before.position.x || camera_.position.y != before.position.y ||
        camera_.position.z != before.position.z || camera_.yaw != before.yaw || camera_.pitch != before.pitch;
}

void CameraRig::scale_speed(float notches)
{
    set_speed(speed() * std::pow(1.25f, notches));
}

void CameraRig::set_speed(float speed)
{
    if (!std::isfinite(speed)) return;
    if (mode_ == CameraMode::walk) walk_speed_ = std::clamp(speed, walk_speed_min, walk_speed_max);
    else fly_speed_ = std::clamp(speed, fly_speed_min, fly_speed_max);
}

bool CameraRig::toggle_walk(const Environment& environment, bool props)
{
    if (mode_ == CameraMode::walk)
    {
        mode_ = CameraMode::fly;
        return true;
    }
    const auto floor = walk_landing(environment, props, camera_.position);
    if (!floor) return false;
    camera_.position.y = *floor + walk_eye_height;
    mode_ = CameraMode::walk;
    return true;
}

bool CameraRig::recall(std::size_t index)
{
    if (index >= bookmark_count) return false;
    const auto& bookmark = bookmarks_[index];
    camera_.position = bookmark.position;
    camera_.yaw = bookmark.yaw;
    camera_.pitch = bookmark.pitch;
    camera_.vertical_fov = bookmark.vertical_fov;
    mode_ = bookmark.mode;
    return true;
}

bool CameraRig::store(std::size_t index)
{
    if (index >= bookmark_count) return false;
    auto& bookmark = bookmarks_[index];
    bookmark.position = camera_.position;
    bookmark.yaw = camera_.yaw;
    bookmark.pitch = camera_.pitch;
    bookmark.vertical_fov = camera_.vertical_fov;
    bookmark.mode = mode_;
    bookmark.stored = true;
    return true;
}
}
