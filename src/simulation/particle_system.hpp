#pragma once

#include "../graphics/graphics_device.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

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
    std::uint32_t reserved[2];
};
static_assert(sizeof(Particle) == particle_layout::particle_stride &&
    offsetof(Particle, position) == particle_layout::particle_position_offset &&
    offsetof(Particle, age) == particle_layout::particle_position_offset + 12 &&
    offsetof(Particle, velocity) == particle_layout::particle_velocity_offset &&
    offsetof(Particle, lifetime) == particle_layout::particle_velocity_offset + 12 &&
    offsetof(Particle, size) == particle_layout::particle_size_offset &&
    offsetof(Particle, generation) == particle_layout::particle_generation_offset);

struct ParticleWork
{
    Dispatch dispatch;
    Draw particles;
    Draw offscreen_smoke;
    Draw light;
    std::uint32_t steps;
    std::uint32_t effects;
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
    static constexpr std::uint32_t emissive_particles = particle_layout::emissive_particles;
    static constexpr std::uint32_t smoke_particles = particle_layout::smoke_particles;
    static constexpr std::uint32_t capacity = effect_slots * slab_records;
    static constexpr std::uint32_t groups_per_slab = slab_records / particle_layout::simulation_group_size;
    static_assert(slab_records % particle_layout::simulation_group_size == 0);

    bool initialize(GraphicsDevice& device, const Buffer& heights, ParticleTargets targets);
    const Buffer& particles() const { return *particles_; }
    std::uint32_t tick() const { return tick_; }
    ParticleWork plan(double elapsed_seconds, std::uint32_t active_effects, std::uint32_t offscreen_divisor = 1);
    void commit(const ParticleWork& work);
    void set_fixed_ticks(bool fixed) { fixed_ticks_ = fixed; }
    void prepare_profile(std::uint32_t tick);
    bool has_pending_steps() const { return pending_steps_ != 0; }
    void discard_pending();
    bool paused() const { return paused_; }
    void toggle_pause();
    void step();

private:
    std::unique_ptr<Buffer> particles_;
    const Buffer* heights_ = nullptr;
    std::unique_ptr<GraphicsPipeline> graphics_;
    std::unique_ptr<GraphicsPipeline> offscreen_graphics_;
    std::unique_ptr<ComputePipeline> compute_;
    std::uint32_t tick_ = 0;
    std::uint32_t pending_steps_ = 0;
    double accumulator_ = 0;
    bool paused_ = false;
    bool fixed_ticks_ = false;
};
}
