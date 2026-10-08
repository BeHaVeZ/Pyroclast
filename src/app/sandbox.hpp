#pragma once

#include "camera_controls.hpp"
#include "explosion_renderer.hpp"
#include "test_range.hpp"
#include "../simulation/explosion_system.hpp"
#include "../simulation/particle_system.hpp"
#include "../simulation/wind.hpp"
#include "../tools/debug_ui.hpp"

#include <cstdint>
#include <optional>
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
    bool debug_lines = false;
    std::uint32_t next_seed = 100000;
    ExplosionPreset click_preset = ExplosionPreset::small_he;
    std::uint32_t layer_mask = particle_layout::full_mask;
    WindSettings wind;
    std::optional<ParticleCounters> counters;
    std::uint64_t counter_sample = 0;

    std::optional<ParticleCounters> poll_counters(const GraphicsDevice& device);
    bool props_shown() const { return ((layer_mask >> particle_layout::props_mask_bit) & 1) != 0; }
    void start_group();
    void trigger_group();
    void clear();
    void spawn(const Ray& ray);
};

void apply(UiCommand command, Sandbox& sandbox);
}
