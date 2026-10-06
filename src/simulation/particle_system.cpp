#include "particle_system.hpp"

#include "../graphics/particle_shaders.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>

namespace pyroclast
{
bool ParticleSystem::initialize(GraphicsDevice& device, Renderer renderer, std::uint32_t explosions)
{
    if (!is_explosion_scenario(explosions))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Explosion scenario must contain 1, 10, 50, or 100 effects.");
        return false;
    }
    explosion_count_ = explosions;
    burst_count_ = explosion_count_ * explosion_particles;
    capacity_ = std::max(minimum_capacity, burst_count_);
    const std::vector<Particle> initial(capacity_);
    particles_ = device.create_vertex_buffer(std::as_bytes(std::span(initial)), sizeof(Particle));
    graphics_ = device.create_graphics_pipeline(particle_shaders(renderer));
    auto smoke = particle_shaders(renderer);
    smoke.blend = BlendMode::premultiplied_alpha;
    smoke_graphics_ = device.create_graphics_pipeline(smoke);
    if (!particles_ || !graphics_ || !smoke_graphics_)
    {
        return false;
    }
    compute_ = device.create_compute_pipeline(particle_compute_shader(renderer), *particles_);
    if (!compute_)
    {
        return false;
    }
    SDL_Log("Explosion ready: explosions=%u, capacity=%u, burst=%u, state=%zu bytes, fixed step=1/60 s, seed=0x12345678.",
        explosion_count_, capacity_, burst_count_, initial.size() * sizeof(Particle));
    return true;
}

RenderResult ParticleSystem::render(GraphicsDevice& device, float aspect_ratio, double elapsed_seconds)
{
    assert(particles_ && graphics_ && smoke_graphics_ && compute_);
    assert(std::isfinite(elapsed_seconds) && elapsed_seconds >= 0);
    assert(std::isfinite(aspect_ratio) && aspect_ratio > 0);
    constexpr double fixed_step = 1.0 / 60.0;
    if (!paused_)
    {
        accumulator_ = std::min(accumulator_ + elapsed_seconds, particle_layout::maximum_simulation_steps * fixed_step);
    }
    const auto steps = paused_ ? std::min(pending_steps_, 1u) :
        static_cast<std::uint32_t>(std::floor(accumulator_ / fixed_step));
    constexpr auto group_size = particle_layout::simulation_group_size;
    const ComputeDispatch dispatch{*compute_,
        {capacity_, steps, burst_count_, reset_epoch_}, (capacity_ + group_size - 1) / group_size};
    const auto emissive_count = explosion_count_ * emissive_particles;
    const DrawRange ranges[] = {
        {*smoke_graphics_, emissive_count, capacity_ - emissive_count},
        {*graphics_, 0, emissive_count}
    };
    const auto result = device.render({*particles_, ranges, {0.04f, 0.07f, 0.12f, 1.0f}, &dispatch, aspect_ratio});
    if (result == RenderResult::submitted)
    {
        if (paused_) pending_steps_ -= steps;
        else accumulator_ = std::max(0.0, accumulator_ - steps * fixed_step);
    }
    return result;
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
