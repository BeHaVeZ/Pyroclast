#pragma once

#include "explosion_scenarios.hpp"
#include "../graphics/graphics_device.hpp"

namespace pyroclast
{
struct Particle
{
    float position[2];
    float velocity[2];
    float age;
    float lifetime;
    float size;
    std::uint32_t reset_epoch;
};
static_assert(sizeof(Particle) == particle_layout::particle_stride &&
    offsetof(Particle, position) == particle_layout::particle_motion_offset &&
    offsetof(Particle, velocity) == particle_layout::particle_motion_offset + 8 &&
    offsetof(Particle, age) == particle_layout::particle_life_offset &&
    offsetof(Particle, lifetime) == particle_layout::particle_life_offset + 4 &&
    offsetof(Particle, size) == particle_layout::particle_life_offset + 8 &&
    offsetof(Particle, reset_epoch) == particle_layout::particle_epoch_offset);

class ParticleSystem final
{
public:
    static constexpr std::uint32_t minimum_capacity = 8192;
    static constexpr std::uint32_t emissive_particles = particle_layout::emissive_particles;
    static constexpr std::uint32_t smoke_particles = particle_layout::smoke_particles;
    static constexpr std::uint32_t explosion_particles = particle_layout::explosion_particles;
    static_assert(explosion_particles <= minimum_capacity);
    bool initialize(GraphicsDevice& device, Renderer renderer, std::uint32_t explosions = 1);
    std::uint32_t capacity() const { return capacity_; }
    std::uint32_t explosion_count() const { return explosion_count_; }
    RenderResult render(GraphicsDevice& device, float aspect_ratio, double elapsed_seconds);
    void prepare_profile(std::uint32_t tick);
    bool has_pending_steps() const { return pending_steps_ != 0; }
    void trigger_explosion();
    void reset();
    void toggle_pause();
    void step();

private:
    void reset_pool(std::uint32_t burst_count);
    std::unique_ptr<VertexBuffer> particles_;
    std::unique_ptr<GraphicsPipeline> graphics_;
    std::unique_ptr<GraphicsPipeline> smoke_graphics_;
    std::unique_ptr<ComputePipeline> compute_;
    std::uint32_t capacity_ = minimum_capacity;
    std::uint32_t explosion_count_ = 1;
    std::uint32_t reset_epoch_ = 1;
    std::uint32_t pending_steps_ = 0;
    double accumulator_ = 0;
    std::uint32_t burst_count_ = explosion_particles;
    bool paused_ = false;
};
}
