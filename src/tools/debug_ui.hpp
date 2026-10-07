#pragma once

#include "../graphics/graphics_device.hpp"
#include "../simulation/particle_system.hpp"

#include <memory>
#include <span>
#include <string_view>
#include <vector>

struct SDL_Window;
union SDL_Event;
struct ImDrawData;

namespace pyroclast
{
struct DiagnosticsSnapshot
{
    std::string_view renderer;
    std::string_view adapter;
    std::string_view build;
    int width = 0;
    int height = 0;
    bool vsync = true;
    std::span<const float> frame_ms;
    int frame_ms_offset = 0;
    std::optional<GpuTimings> gpu;
    std::uint32_t explosions = 0;
    std::uint32_t active_effects = 0;
    std::uint32_t effect_slots = 0;
    std::uint64_t effects_started = 0;
    std::uint64_t effects_dropped = 0;
    std::uint32_t tick = 0;
    std::optional<ParticleCounters> particles;
    bool debug_lines = false;
    bool paused = false;
    bool half_resolution_smoke = false;
    std::string_view scene_format;
    float exposure = 1;
    float camera_position[3]{};
    float camera_yaw = 0;
    float camera_pitch = 0;
    float camera_speed = 0;
};

enum class UiCommand { trigger, clear, toggle_pause, step, toggle_debug_lines };

class DebugUi final
{
public:
    static std::unique_ptr<DebugUi> create(SDL_Window* window, Renderer renderer);
    ~DebugUi();
    DebugUi(const DebugUi&) = delete;
    DebugUi& operator=(const DebugUi&) = delete;

    // True when the overlay used the event, so application controls must ignore it.
    bool process_event(const SDL_Event& event);
    void toggle_visible() { visible_ = !visible_; }
    bool visible() const { return visible_; }
    bool wants_keyboard() const;
    ImDrawData* build(const DiagnosticsSnapshot& snapshot, std::vector<UiCommand>& commands);

private:
    DebugUi() = default;
    bool context_ = false;
    bool platform_ = false;
    bool visible_ = true;
};
}
