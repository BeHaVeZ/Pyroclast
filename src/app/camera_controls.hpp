#pragma once

#include "camera.hpp"
#include "../simulation/environment.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace pyroclast
{
enum class CameraMode
{
    fly,
    walk,
};

inline constexpr float walk_eye_height = 1.7f;
inline constexpr float walk_radius = 0.3f;
inline constexpr float walk_knee_height = 0.55f;
inline constexpr float walk_step_height = 0.5f;
inline constexpr float walk_substep = 0.25f;
inline constexpr float walk_edge_margin = 1.0f;

Ray crosshair_ray(const Camera& camera);

float walk_floor(const Environment& environment, bool props, float x, float z, float feet_y);
float3 walk_move(const Environment& environment, bool props, float3 feet, float3 displacement);
std::optional<float> walk_landing(const Environment& environment, bool props, float3 eye);

struct CameraBookmark
{
    float3 position;
    float yaw = 0;
    float pitch = 0;
    CameraMode mode = CameraMode::fly;
    std::string_view name;
    bool stored = false;
};

class CameraRig final
{
public:
    static constexpr std::size_t bookmark_count = 6;
    static constexpr float fly_speed_min = 1;
    static constexpr float fly_speed_max = 400;
    static constexpr float walk_speed_min = 1;
    static constexpr float walk_speed_max = 20;

    CameraRig() = default;
    CameraRig(const Camera& start, const std::array<Camera, bookmark_count>& views,
        const std::array<std::string_view, bookmark_count>& names);

    const Camera& camera() const { return camera_; }
    CameraMode mode() const { return mode_; }
    float speed() const { return mode_ == CameraMode::walk ? walk_speed_ : fly_speed_; }
    const std::array<CameraBookmark, bookmark_count>& bookmarks() const { return bookmarks_; }

    bool update(const CameraInput& input, float seconds, const Environment& environment, bool props);
    void scale_speed(float notches);
    void set_speed(float speed);
    bool toggle_walk(const Environment& environment, bool props);
    bool recall(std::size_t index);
    bool store(std::size_t index);

private:
    Camera camera_;
    CameraMode mode_ = CameraMode::fly;
    float fly_speed_ = 15;
    float walk_speed_ = 5;
    std::array<CameraBookmark, bookmark_count> bookmarks_{};
};
}
