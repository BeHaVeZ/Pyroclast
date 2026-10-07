#include "particle_system.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cassert>
#include <cmath>

namespace pyroclast
{
bool ParticleSystem::initialize(GraphicsDevice& device, const Buffer& heights, ParticleTargets targets)
{
    heights_ = &heights;
    const auto bytes = capacity * static_cast<std::uint32_t>(sizeof(Particle));
    particles_ = device.create_buffer(bytes, {});
    graphics_ = device.create_graphics_pipeline(
        {Shader::particle_vertex, Shader::particle_pixel, BlendMode::premultiplied_alpha, DepthMode::test, targets.scene});
    offscreen_graphics_ = device.create_graphics_pipeline(
        {Shader::particle_vertex, Shader::particle_pixel, BlendMode::premultiplied_alpha, DepthMode::none, targets.offscreen});
    compute_ = device.create_compute_pipeline(Shader::particle_simulation);
    if (!particles_ || !graphics_ || !offscreen_graphics_ || !compute_)
    {
        return false;
    }
    SDL_Log("Particle pool ready: %u effect slots of %u particles, %u bytes, fixed step=1/60 s.", effect_slots, slab_records, bytes);
    return true;
}

namespace
{
constexpr double fixed_step = 1.0 / 60.0;
}

ParticleWork ParticleSystem::plan(double elapsed_seconds, std::uint32_t active_effects, std::uint32_t offscreen_divisor)
{
    assert(particles_ && heights_ && graphics_ && offscreen_graphics_ && compute_);
    assert(std::isfinite(elapsed_seconds) && elapsed_seconds >= 0);
    assert(active_effects <= effect_slots && offscreen_divisor >= 1);
    if (!paused_ && !fixed_ticks_)
    {
        accumulator_ = std::min(accumulator_ + elapsed_seconds, particle_layout::maximum_simulation_steps * fixed_step);
    }
    const auto steps = paused_ ? std::min(pending_steps_, 1u) : fixed_ticks_ ? 1u :
        static_cast<std::uint32_t>(std::floor(accumulator_ / fixed_step));
    Bindings simulation{};
    simulation.read[0] = heights_;
    simulation.write[0] = particles_.get();
    Bindings drawing{};
    drawing.read[0] = particles_.get();
    const DrawConstants parameters{steps, active_effects, capacity, 0};
    const DrawConstants both_layers{capacity, active_effects, 0, slab_records, 1};
    const DrawConstants smoke_layer{capacity, active_effects, 0, smoke_particles, offscreen_divisor};
    const DrawConstants light_layer{capacity, active_effects, smoke_particles, emissive_particles, 1};
    return {
        {*compute_, simulation, parameters, {std::max(active_effects * groups_per_slab, 1u), 1, 1}, "Particle simulation"},
        {*graphics_, drawing, both_layers, 6, active_effects * slab_records, "Particles"},
        {*offscreen_graphics_, drawing, smoke_layer, 6, active_effects * smoke_particles, "Smoke"},
        {*graphics_, drawing, light_layer, 6, active_effects * emissive_particles, "Light"},
        steps, active_effects};
}

void ParticleSystem::commit(const ParticleWork& work)
{
    tick_ += work.steps;
    if (paused_) pending_steps_ -= std::min(pending_steps_, work.steps);
    else if (!fixed_ticks_) accumulator_ = std::max(0.0, accumulator_ - work.steps * fixed_step);
}

void ParticleSystem::prepare_profile(std::uint32_t tick)
{
    assert(particles_ && tick_ == 0 && tick <= 480);
    paused_ = true;
    accumulator_ = 0;
    pending_steps_ = tick;
}

void ParticleSystem::discard_pending()
{
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
