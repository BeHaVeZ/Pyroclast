#pragma once

#include "../simulation/explosion_presets.hpp"
#include "../graphics/graphics_device.hpp"
#include "../graphics/realism_layout.hpp"

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
    Draw offscreen_particles;
    Draw debris;
    Frame::Readback counters;
    std::uint32_t steps;
    std::uint32_t effects;
    std::uint32_t units = 0;
};

struct FrameUnits
{
    std::uint32_t units = 0;
    std::uint32_t groups = 0;
    bool rotor = false;
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
    std::uint32_t residual = 0;
    std::uint32_t flags = 0;
    std::uint32_t ghosts = 0;
    bool measured = false;
};

struct ParticleListing
{
    bool measure = false;
    bool offscreen_light = false;
    float lod_pixels = 0;
    float faint_floor = 0;
};

struct ParticleTargets
{
    TargetFormat scene;
    TargetFormat offscreen;
};

enum class ParticleModel : std::uint8_t { legacy, realism, rotor };
inline constexpr std::size_t particle_model_count = 3;

class ParticleSystem final
{
public:
    static constexpr std::uint32_t effect_slots = particle_layout::maximum_effects;
    static constexpr std::uint32_t slab_records = particle_layout::explosion_particles;
    static constexpr std::uint32_t capacity = effect_slots * slab_records;
    static constexpr std::uint32_t pool_records = realism_layout::pool_records;
    static_assert(pool_records == capacity + realism_layout::residual_records);
    static constexpr std::uint32_t groups_per_slab = particle_layout::slab_groups;
    static_assert(slab_records % particle_layout::simulation_group_size == 0);

    bool initialize(GraphicsDevice& device, const Buffer& environment, ParticleTargets targets, std::span<const EffectClass> classes);
    const Buffer& particles() const { return *particles_; }
    ParticleWork plan(std::uint32_t tick, std::uint32_t steps, std::uint32_t active_effects, std::uint32_t offscreen_divisor = 0,
        ParticleListing listing = {}, const FrameUnits* units = nullptr) const;
    static std::optional<ParticleCounters> counters(const ReadbackData& data);

private:
    struct DrawPipelines
    {
        std::unique_ptr<GraphicsPipeline> scene;
        std::unique_ptr<GraphicsPipeline> offscreen;
        std::unique_ptr<GraphicsPipeline> lod_scene;
        std::unique_ptr<GraphicsPipeline> lod_offscreen;
    };
    struct ComputePipelines
    {
        std::unique_ptr<ComputePipeline> simulation;
        std::unique_ptr<ComputePipeline> scan;
        std::unique_ptr<ComputePipeline> scatter;
        std::unique_ptr<ComputePipeline> scatter_measure;
    };

    std::unique_ptr<Buffer> particles_;
    std::unique_ptr<Buffer> alive_masks_;
    std::unique_ptr<Buffer> offsets_;
    std::unique_ptr<Buffer> alive_;
    std::unique_ptr<Buffer> arguments_;
    std::unique_ptr<Buffer> classes_;
    const Buffer* environment_ = nullptr;
    std::array<DrawPipelines, particle_model_count> draws_;
    std::array<ComputePipelines, particle_model_count> computes_;
    std::unique_ptr<GraphicsPipeline> debris_;
};
}
