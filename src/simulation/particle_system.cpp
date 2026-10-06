#include "particle_system.hpp"

#include "../graphics/particle_shaders.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>

namespace pyroclast
{
bool ParticleSystem::initialize(GraphicsDevice& device, Renderer renderer)
{
    const std::vector<Particle> initial(capacity);
    particles_ = device.create_vertex_buffer(std::as_bytes(std::span(initial)), VertexLayout::particle);
    graphics_ = device.create_graphics_pipeline(particle_shaders(renderer));
    if (!particles_ || !graphics_)
    {
        return false;
    }
    compute_ = device.create_compute_pipeline(particle_compute_shader(renderer), *particles_);
    SDL_Log("Particle baseline: capacity=%u, state=%zu bytes, fixed step=1/60 s, seed=0x12345678.",
        capacity, initial.size() * sizeof(Particle));
    return compute_ != nullptr;
}

RenderResult ParticleSystem::render(GraphicsDevice& device, float aspect_ratio, double elapsed_seconds)
{
    assert(particles_ && graphics_ && compute_);
    assert(std::isfinite(elapsed_seconds) && elapsed_seconds >= 0);
    assert(std::isfinite(aspect_ratio) && aspect_ratio > 0);
    constexpr double fixed_step = 1.0 / 60.0;
    if (!paused_)
    {
        accumulator_ = std::min(accumulator_ + elapsed_seconds, 4 * fixed_step);
    }
    const auto steps = paused_ ? std::min(pending_steps_, 1u) :
        static_cast<std::uint32_t>(std::floor(accumulator_ / fixed_step));
    const ComputeDispatch dispatch{*compute_,
        {capacity, steps, emitting_ ? 1u : 0u, reset_epoch_}, (capacity + 63) / 64};
    const auto result = device.render({*particles_, *graphics_, {0.04f, 0.07f, 0.12f, 1.0f}, &dispatch, aspect_ratio});
    if (result == RenderResult::submitted)
    {
        if (paused_) pending_steps_ -= steps;
        else accumulator_ = std::max(0.0, accumulator_ - steps * fixed_step);
    }
    return result;
}

void ParticleSystem::toggle_emission()
{
    emitting_ = !emitting_;
    SDL_Log("Particle emission: %s", emitting_ ? "on" : "off (existing particles drain)");
}

void ParticleSystem::reset()
{
    ++reset_epoch_;
    pending_steps_ = 0;
    accumulator_ = 0;
    SDL_Log("Particle pool reset requested.");
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
