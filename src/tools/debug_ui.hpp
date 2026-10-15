#pragma once

#include "../core/math.hpp"
#include "../graphics/graphics_device.hpp"
#include "../render/particle_system.hpp"
#include "../simulation/pilot_control.hpp"
#include "../simulation/wind.hpp"

#include <array>
#include <memory>
#include <optional>
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
    struct System
    {
        std::string_view renderer;
        std::string_view adapter;
        std::string_view build;
        int width = 0;
        int height = 0;
        bool vsync = true;
        std::string_view scene_format;
        float exposure = 1;
    };
    struct Frame
    {
        std::span<const float> cpu_ms;
        int cpu_offset = 0;
        std::span<const float> gpu_ms;
        int gpu_offset = 0;
        std::optional<GpuTimings> gpu;
        double showcase_cpu_ms = 0;
    };
    struct Clock
    {
        std::uint32_t tick = 0;
        bool paused = false;
        float time_scale = 1;
        std::uint32_t steps = 0;
        float ticks_per_second = 0;
        std::uint64_t capped_frames = 0;
    };
    struct Load
    {
        std::uint32_t explosions = 0;
        std::uint32_t active_effects = 0;
        std::uint32_t effect_slots = 0;
        std::uint64_t started = 0;
        std::uint64_t dropped = 0;
        std::uint64_t dropped_since_clear = 0;
        std::uint64_t dropped_recent = 0;
        std::uint32_t peak_since_clear = 0;
        std::size_t queued = 0;
        std::array<std::uint32_t, preset_count> active_by_preset{};
        std::uint32_t simulated_records = 0;
        std::uint32_t pool_capacity = 0;
        std::optional<ParticleCounters> particles;
        std::uint32_t lights = 0;
        bool coverage = false;
        bool coverage_flag = false;
        std::string_view smoke_resolution = "full";
        bool smoke_half = false;
        bool half_light = false;
        bool smoke_count_budget = false;
        double smoke_limit = 0;
        double smoke_load = 0;
        std::uint64_t smoke_switches = 0;
        float smoke_lod = 0;
        float faint_floor = 0;
        std::string_view effect_order = "origin";
        std::string_view slot_policy = "drop-new";
        std::uint64_t evicted = 0;
        std::uint64_t deferred = 0;
        std::size_t waiting = 0;
        bool realism = false;
        std::uint32_t clusters = 0;
        std::uint64_t forced_fades = 0;
        std::uint32_t linger_scale = 0;
        std::uint32_t linger_seconds = 0;
        std::uint32_t rotor_slots = 0;
        std::uint32_t jet_slots = 0;
        struct Helicopter
        {
            std::string_view state;
            float hub_height = 0;
            std::uint32_t emission = 0;
            std::uint32_t dust = 0;
            bool waiting = false;
        };
        std::optional<Helicopter> helicopter;
        std::string_view jet;
        struct Pilot
        {
            std::string_view aircraft;
            float speed = 0;
            float height = 0;
            float load = 0;
            float bank = 0;
            float mach = 0;
            bool chase = true;
            float scene_distance = 0;
            float scene_bearing = 0;
            bool returning = false;
        };
        std::optional<Pilot> pilot;
        std::array<float, pilot_setting_count> pilot_settings{};
    };
    struct Aim
    {
        float3 point;
        float distance = 0;
        std::string_view surface;
        std::string_view prop;
        std::string_view material;
    };
    struct Showcase
    {
        ExplosionPreset preset = ExplosionPreset::small_he;
        std::uint32_t count = 1;
        std::uint32_t preset_ticks = 0;
        std::optional<Aim> aim;
        std::optional<std::uint32_t> barrage_progress;
        std::optional<std::uint32_t> bombardment_impacts;
        std::uint32_t bombardment_interval = 15;
        float bombardment_radius = 30;
    };
    struct Camera
    {
        float position[3]{};
        float yaw = 0;
        float pitch = 0;
        float speed = 0;
        bool walking = false;
        std::array<std::string_view, 7> bookmarks{};
        std::array<bool, 7> stored{};
    };
    struct Wind
    {
        WindSettings settings;
        float now_speed = 0;
        float now_heading = 0;
        float right = 0;
        float forward = 0;
        float north_right = 0;
        float north_forward = 1;
    };
    struct Views
    {
        bool effect_bounds = false;
        bool collision_boxes = false;
        bool spawn_regions = false;
        bool wind_arrow = false;
    };

    System system;
    Frame frame;
    Clock clock;
    Load load;
    Showcase showcase;
    Camera camera;
    Wind wind;
    Views views;
    std::uint32_t layer_mask = particle_layout::full_mask;
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
        set_count,
        fire,
        barrage,
        toggle_bombardment,
        set_bombardment,
        toggle_debug_view,
        toggle_coverage,
        set_smoke_resolution,
        set_smoke_budget,
        toggle_half_light,
        set_smoke_lod,
        set_faint_floor,
        set_effect_order,
        set_slot_policy,
        send_helicopter,
        send_jet,
        pilot_helicopter,
        pilot_jet,
        release_pilot,
        toggle_chase,
        return_pilot,
        set_pilot_setting,
        reset_pilot_settings,
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
    float radius_edit_ = 0;
    bool radius_editing_ = false;
    float budget_edit_ = 0;
    bool budget_editing_ = false;
};
}
