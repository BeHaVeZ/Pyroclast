#pragma once

#include "explosion_presets.hpp"
#include "../graphics/graphics_device.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace pyroclast
{
struct Particle
{
    float position[3];
    float age;
    float velocity[3];
    float lifetime;
    float size;
    std::uint32_t generation;
    float spin;
    std::uint32_t reserved;
};
static_assert(sizeof(Particle) == particle_layout::particle_stride &&
    offsetof(Particle, position) == particle_layout::particle_position_offset &&
    offsetof(Particle, age) == particle_layout::particle_position_offset + 12 &&
    offsetof(Particle, velocity) == particle_layout::particle_velocity_offset &&
    offsetof(Particle, lifetime) == particle_layout::particle_velocity_offset + 12 &&
    offsetof(Particle, size) == particle_layout::particle_size_offset &&
    offsetof(Particle, generation) == particle_layout::particle_generation_offset &&
    offsetof(Particle, spin) == particle_layout::particle_spin_offset);

struct ParticleWork
{
    std::array<Dispatch, 3> dispatches;
    Draw particles;
    Draw offscreen_smoke;
    Draw light;
    Draw debris;
    Frame::Readback counters;
    std::uint32_t steps;
    std::uint32_t effects;
};

struct ParticleCounters
{
    std::uint32_t tick = 0;
    std::uint32_t alive = 0;
    std::uint32_t smoke = 0;
    std::uint32_t light = 0;
    std::uint32_t debris = 0;
    std::uint32_t offscreen = 0;
    std::uint32_t thinned = 0;
    std::uint32_t faint = 0;
    std::array<std::uint32_t, 4> smoke_coverage{};
    std::uint32_t light_coverage = 0;
    std::uint32_t reserved[3]{};
};

struct ParticleListing
{
    bool measure = false;
};

struct ParticleTargets
{
    TargetFormat scene;
    TargetFormat offscreen;
};

class ParticleSystem final
{
public:
    static constexpr std::uint32_t effect_slots = particle_layout::maximum_effects;
    static constexpr std::uint32_t slab_records = particle_layout::explosion_particles;
    static constexpr std::uint32_t capacity = effect_slots * slab_records;
    static constexpr std::uint32_t groups_per_slab = particle_layout::slab_groups;
    static_assert(slab_records % particle_layout::simulation_group_size == 0);

    bool initialize(GraphicsDevice& device, const Buffer& environment, ParticleTargets targets, std::span<const EffectClass> classes);
    const Buffer& particles() const { return *particles_; }
    std::uint32_t tick() const { return tick_; }
    ParticleWork plan(double elapsed_seconds, std::uint32_t active_effects, std::uint32_t offscreen_divisor = 0,
        ParticleListing listing = {});
    static std::optional<ParticleCounters> counters(const ReadbackData& data);
    void commit(const ParticleWork& work);
    void set_fixed_ticks(bool fixed) { fixed_ticks_ = fixed; }
    void prepare_profile(std::uint32_t tick);
    bool has_pending_steps() const { return pending_steps_ != 0; }
    void discard_pending();
    bool paused() const { return paused_; }
    void toggle_pause();
    void step();
    void set_time_scale(double scale);
    double time_scale() const { return time_scale_; }
    std::uint64_t capped_frames() const { return capped_frames_; }

private:
    std::unique_ptr<Buffer> particles_;
    std::unique_ptr<Buffer> alive_masks_;
    std::unique_ptr<Buffer> offsets_;
    std::unique_ptr<Buffer> alive_;
    std::unique_ptr<Buffer> arguments_;
    std::unique_ptr<Buffer> classes_;
    const Buffer* environment_ = nullptr;
    std::unique_ptr<GraphicsPipeline> graphics_;
    std::unique_ptr<GraphicsPipeline> offscreen_graphics_;
    std::unique_ptr<GraphicsPipeline> debris_graphics_;
    std::unique_ptr<ComputePipeline> compute_;
    std::unique_ptr<ComputePipeline> scan_;
    std::unique_ptr<ComputePipeline> scatter_;
    std::unique_ptr<ComputePipeline> scatter_measure_;
    std::uint32_t tick_ = 0;
    std::uint32_t pending_steps_ = 0;
    double accumulator_ = 0;
    double time_scale_ = 1;
    std::uint64_t capped_frames_ = 0;
    bool paused_ = false;
    bool fixed_ticks_ = false;
};
}
