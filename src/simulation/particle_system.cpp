#include "particle_system.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cassert>
#include <cmath>

namespace pyroclast
{
bool ParticleSystem::initialize(GraphicsDevice& device, const Terrain& terrain, const Buffer& heights, std::uint32_t explosions,
    ParticleTargets targets)
{
    if (!is_explosion_scenario(explosions))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Explosion scenario must contain 1, 10, 50, or 100 effects.");
        return false;
    }
    explosion_count_ = explosions;
    burst_count_ = explosion_count_ * explosion_particles;
    capacity_ = std::max(minimum_capacity, burst_count_);
    heights_ = &heights;
    const std::uint32_t columns = explosions == 1 ? 1 : explosions == 10 ? 5 : 10;
    const std::uint32_t rows = (explosions + columns - 1) / columns;
    for (std::uint32_t effect = 0; effect < explosions; ++effect)
    {
        const float x = (static_cast<float>(effect % columns) - 0.5f * static_cast<float>(columns - 1)) * effect_spacing;
        const float z = (static_cast<float>(effect / columns) - 0.5f * static_cast<float>(rows - 1)) * effect_spacing;
        origins_[effect] = {x, terrain.height(x, z) + 0.5f, z, 0};
    }
    const auto bytes = capacity_ * static_cast<std::uint32_t>(sizeof(Particle));
    particles_ = device.create_buffer(bytes, {});
    graphics_ = device.create_graphics_pipeline(
        {Shader::particle_vertex, Shader::particle_pixel, BlendMode::premultiplied_alpha, DepthMode::test, targets.scene});
    offscreen_graphics_ = device.create_graphics_pipeline(
        {Shader::particle_vertex, Shader::particle_pixel, BlendMode::premultiplied_alpha, DepthMode::none, targets.offscreen});
    if (!particles_ || !graphics_ || !offscreen_graphics_)
    {
        return false;
    }
    compute_ = device.create_compute_pipeline(Shader::particle_simulation);
    if (!compute_)
    {
        return false;
    }
    SDL_Log("Explosion ready: explosions=%u, capacity=%u, burst=%u, state=%u bytes, fixed step=1/60 s, seed=0x12345678.",
        explosion_count_, capacity_, burst_count_, bytes);
    return true;
}

namespace
{
constexpr double fixed_step = 1.0 / 60.0;
}

ParticleWork ParticleSystem::plan(double elapsed_seconds, std::uint32_t offscreen_divisor)
{
    assert(particles_ && heights_ && graphics_ && offscreen_graphics_ && compute_);
    assert(std::isfinite(elapsed_seconds) && elapsed_seconds >= 0);
    if (!paused_)
    {
        accumulator_ = std::min(accumulator_ + elapsed_seconds, particle_layout::maximum_simulation_steps * fixed_step);
    }
    const auto steps = paused_ ? std::min(pending_steps_, 1u) :
        static_cast<std::uint32_t>(std::floor(accumulator_ / fixed_step));
    constexpr auto group_size = particle_layout::simulation_group_size;
    Bindings simulation{};
    simulation.read[0] = heights_;
    simulation.write[0] = particles_.get();
    Bindings drawing{};
    drawing.read[0] = particles_.get();
    const DrawConstants parameters{capacity_, steps, burst_count_, reset_epoch_};
    assert(explosion_count_ * explosion_particles <= capacity_ && particles_->size() == capacity_ * particle_layout::particle_stride);
    assert(offscreen_divisor >= 1);
    const DrawConstants both_layers{capacity_, explosion_count_, 0, explosion_particles, 1};
    const DrawConstants smoke_layer{capacity_, explosion_count_, 0, smoke_particles, offscreen_divisor};
    const DrawConstants light_layer{capacity_, explosion_count_, smoke_particles, emissive_particles, 1};
    return {
        {*compute_, simulation, parameters, {(capacity_ + group_size - 1) / group_size, 1, 1}, "Particle simulation"},
        {*graphics_, drawing, both_layers, 6, explosion_count_ * explosion_particles, "Particles"},
        {*offscreen_graphics_, drawing, smoke_layer, 6, explosion_count_ * smoke_particles, "Smoke"},
        {*graphics_, drawing, light_layer, 6, explosion_count_ * emissive_particles, "Light"},
        steps};
}

void ParticleSystem::commit(const ParticleWork& work)
{
    if (paused_) pending_steps_ -= std::min(pending_steps_, work.steps);
    else accumulator_ = std::max(0.0, accumulator_ - work.steps * fixed_step);
}

void ParticleSystem::prepare_profile(std::uint32_t tick)
{
    assert(particles_ && reset_epoch_ == 1 && tick <= 480);
    paused_ = true;
    accumulator_ = 0;
    pending_steps_ = tick;
}

void ParticleSystem::trigger_explosion()
{
    reset_pool(explosion_count_ * explosion_particles);
    SDL_Log("Explosion group triggered: %u effects, %u particles; replaces the previous group.", explosion_count_, burst_count_);
}

void ParticleSystem::reset()
{
    reset_pool(0);
    SDL_Log("Particle pool cleared.");
}

void ParticleSystem::reset_pool(std::uint32_t burst_count)
{
    assert(burst_count <= capacity_);
    ++reset_epoch_;
    burst_count_ = burst_count;
    pending_steps_ = 0;
    accumulator_ = 0;
}

void ParticleSystem::toggle_pause()
{
    paused_ = !paused_;
    pending_steps_ = 0;
    accumulator_ = 0;
    SDL_Log("Particle simulation: %s", paused_ ? "paused" : "running");
}

void ParticleSystem::step()
{
    if (paused_ && pending_steps_ < 600)
    {
        ++pending_steps_;
    }
}
}
