#pragma once

#include "camera_controls.hpp"
#include "explosion_renderer.hpp"
#include "showcase.hpp"
#include "test_range.hpp"
#include "../simulation/explosion_system.hpp"
#include "../simulation/particle_system.hpp"
#include "../simulation/wind.hpp"
#include "../tools/debug_ui.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace pyroclast
{
struct Sandbox
{
    TestRange range;
    ParticleSystem particles;
    ExplosionSystem effects{ParticleSystem::effect_slots};
    ExplosionRenderer renderer;
    std::vector<ExplosionEvent> group;
    CameraRig camera;
    DebugViews views;
    std::optional<SurfaceHit> aim;
    std::vector<ShowcaseMark> marks;
    Showcase showcase;
    SlotPressure pressure;
    std::uint32_t layer_mask = particle_layout::full_mask;
    WindSettings wind;
    ScalabilitySettings scalability;
    LingerSettings linger;
    float humidity = 0.8f;
    Rotorcraft rotorcraft;
    Jetcraft jetcraft;
    std::optional<float3> last_click;
    std::uint32_t flights = 0;
    std::optional<ParticleCounters> counters;
    std::uint64_t counter_sample = 0;

    std::optional<ParticleCounters> poll_counters(const GraphicsDevice& device);
    bool props_shown() const { return ((layer_mask >> particle_layout::props_mask_bit) & 1) != 0; }
    void start_group();
    void trigger_group();
    void clear();
    void spawn(const Ray& ray, std::string_view source);
    void send_helicopter();
    void send_jet();
    void release_due();
    void update_aim(bool needed);
    void build_marks();
    void fill_snapshot(DiagnosticsSnapshot& snapshot) const;
    void report_drops(const SlotPressure::Report& report, bool before_clear = false) const;
};

void apply(UiCommand command, Sandbox& sandbox);
}
