#pragma once

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
static_assert(sizeof(Particle) == vertex_stride(VertexLayout::particle) &&
    offsetof(Particle, position) == 0 && offsetof(Particle, velocity) == 8 &&
    offsetof(Particle, age) == 16 && offsetof(Particle, lifetime) == 20 &&
    offsetof(Particle, size) == 24 && offsetof(Particle, reset_epoch) == 28);

class ParticleSystem final
{
public:
    static constexpr std::uint32_t minimum_capacity = 8192;
    static constexpr std::uint32_t maximum_explosions = 100;
    static constexpr std::uint32_t emissive_particles = 2048;
    static constexpr std::uint32_t smoke_particles = 512;
    static constexpr std::uint32_t explosion_particles = emissive_particles + smoke_particles;
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
