#pragma once

#include "../graphics/graphics_device.hpp"
#include "../simulation/particle_system.hpp"
#include "../simulation/wind.hpp"

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
    ExplosionPreset click_preset = ExplosionPreset::small_he;
    std::uint32_t layer_mask = particle_layout::full_mask;
    std::uint32_t lights = 0;
    WindSettings wind;
    float time_scale = 1;
    std::uint64_t capped_frames = 0;
};

struct UiCommand
{
    enum class Kind
    {
        trigger,
        clear,
        toggle_pause,
        step,
        toggle_debug_lines,
        select_preset,
        toggle_layer,
        set_wind,
        toggle_slow_motion,
        toggle_walk,
        recall_bookmark,
        store_bookmark,
        set_camera_speed,
    };
    Kind kind;
    std::uint32_t value = 0;
    WindSettings wind;
    float scalar = 0;
};

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
