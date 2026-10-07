#pragma once

#include "explosion_scenarios.hpp"
#include "terrain.hpp"
#include "../graphics/graphics_device.hpp"

#include <array>
#include <span>

namespace pyroclast
{
struct Particle
{
    float position[3];
    float age;
    float velocity[3];
    float lifetime;
    float size;
    std::uint32_t reset_epoch;
    std::uint32_t reserved[2];
};
static_assert(sizeof(Particle) == particle_layout::particle_stride &&
    offsetof(Particle, position) == particle_layout::particle_position_offset &&
    offsetof(Particle, age) == particle_layout::particle_position_offset + 12 &&
    offsetof(Particle, velocity) == particle_layout::particle_velocity_offset &&
    offsetof(Particle, lifetime) == particle_layout::particle_velocity_offset + 12 &&
    offsetof(Particle, size) == particle_layout::particle_size_offset &&
    offsetof(Particle, reset_epoch) == particle_layout::particle_epoch_offset);

struct ParticleWork
{
    Dispatch dispatch;
    Draw particles;
    Draw offscreen_smoke;
    Draw light;
    std::uint32_t steps;
};

class ParticleSystem final
{
public:
    static constexpr std::uint32_t minimum_capacity = 8192;
    static constexpr std::uint32_t emissive_particles = particle_layout::emissive_particles;
    static constexpr std::uint32_t smoke_particles = particle_layout::smoke_particles;
    static constexpr std::uint32_t explosion_particles = particle_layout::explosion_particles;
    static constexpr float effect_spacing = 9;
    static_assert(explosion_particles <= minimum_capacity);
    bool initialize(GraphicsDevice& device, const Terrain& terrain, const Buffer& heights, std::uint32_t explosions = 1);
    std::uint32_t capacity() const { return capacity_; }
    std::uint32_t explosion_count() const { return explosion_count_; }
    const Buffer& particles() const { return *particles_; }
    std::span<const float4> effect_origins() const { return {origins_.data(), explosion_count_}; }
    ParticleWork plan(double elapsed_seconds);
    void commit(const ParticleWork& work);
    void prepare_profile(std::uint32_t tick);
    bool has_pending_steps() const { return pending_steps_ != 0; }
    bool paused() const { return paused_; }
    void trigger_explosion();
    void reset();
    void toggle_pause();
    void step();

private:
    void reset_pool(std::uint32_t burst_count);
    std::unique_ptr<Buffer> particles_;
    const Buffer* heights_ = nullptr;
    std::unique_ptr<GraphicsPipeline> graphics_;
    std::unique_ptr<GraphicsPipeline> offscreen_graphics_;
    std::unique_ptr<ComputePipeline> compute_;
    std::array<float4, maximum_explosions> origins_{};
    std::uint32_t capacity_ = minimum_capacity;
    std::uint32_t explosion_count_ = 1;
    std::uint32_t reset_epoch_ = 1;
    std::uint32_t pending_steps_ = 0;
    double accumulator_ = 0;
    std::uint32_t burst_count_ = explosion_particles;
    bool paused_ = false;
};
}
