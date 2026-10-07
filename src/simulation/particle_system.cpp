#include "particle_system.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

namespace pyroclast
{
bool ParticleSystem::initialize(GraphicsDevice& device, const Buffer& heights, ParticleTargets targets,
    std::span<const EffectClass> classes)
{
    assert(!classes.empty() && classes.size() <= particle_layout::maximum_effect_classes);
    heights_ = &heights;
    const auto bytes = capacity * static_cast<std::uint32_t>(sizeof(Particle));
    particles_ = device.create_buffer(bytes, {});
    constexpr std::uint32_t groups = effect_slots * groups_per_slab;
    alive_masks_ = device.create_buffer(groups * 8, {});
    offsets_ = device.create_buffer(groups * 4, {});
    alive_ = device.create_buffer(3 * capacity * 4, {});
    std::vector<EffectClass> table(particle_layout::maximum_effect_classes);
    std::copy(classes.begin(), classes.end(), table.begin());
    classes_ = device.create_buffer(static_cast<std::uint32_t>(table.size() * sizeof(EffectClass)), std::as_bytes(std::span(table)));
    arguments_ = device.create_buffer(particle_layout::arguments_bytes, {});
    graphics_ = device.create_graphics_pipeline(
        {Shader::particle_vertex, Shader::particle_pixel, BlendMode::premultiplied_alpha, DepthMode::test, targets.scene});
    offscreen_graphics_ = device.create_graphics_pipeline(
        {Shader::particle_vertex, Shader::particle_pixel, BlendMode::premultiplied_alpha, DepthMode::none, targets.offscreen});
    debris_graphics_ = device.create_graphics_pipeline(
        {Shader::debris_vertex, Shader::debris_pixel, BlendMode::opaque, DepthMode::test_write, targets.scene});
    compute_ = device.create_compute_pipeline(Shader::particle_simulation);
    scan_ = device.create_compute_pipeline(Shader::particle_scan);
    scatter_ = device.create_compute_pipeline(Shader::particle_scatter);
    if (!particles_ || !alive_masks_ || !offsets_ || !alive_ || !arguments_ || !classes_ || !graphics_ || !offscreen_graphics_ ||
        !debris_graphics_ || !compute_ || !scan_ || !scatter_)
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
    assert(particles_ && heights_ && graphics_ && offscreen_graphics_ && debris_graphics_ && compute_ && scan_ && scatter_);
    assert(std::isfinite(elapsed_seconds) && elapsed_seconds >= 0);
    assert(active_effects <= effect_slots);
    if (!paused_ && !fixed_ticks_)
    {
        accumulator_ = std::min(accumulator_ + elapsed_seconds, particle_layout::maximum_simulation_steps * fixed_step);
    }
    const auto steps = paused_ ? std::min(pending_steps_, 1u) : fixed_ticks_ ? 1u :
        static_cast<std::uint32_t>(std::floor(accumulator_ / fixed_step));
    Bindings simulation{};
    simulation.read[0] = heights_;
    simulation.read[1] = classes_.get();
    simulation.write[0] = particles_.get();
    simulation.write[1] = alive_masks_.get();
    Bindings scan{};
    scan.read[0] = alive_masks_.get();
    scan.read[1] = classes_.get();
    scan.write[0] = offsets_.get();
    scan.write[1] = arguments_.get();
    Bindings scatter{};
    scatter.read[0] = alive_masks_.get();
    scatter.read[1] = offsets_.get();
    scatter.read[2] = classes_.get();
    scatter.write[0] = alive_.get();
    Bindings drawing{};
    drawing.read[0] = particles_.get();
    drawing.read[1] = alive_.get();
    drawing.read[2] = classes_.get();
    const auto groups = std::max(active_effects * groups_per_slab, 1u);
    const bool layered = offscreen_divisor != 0;
    constexpr auto light_first = particle_layout::light_list_first;
    const DrawConstants parameters{steps, active_effects, capacity, tick_};
    const DrawConstants scan_parameters{active_effects * groups_per_slab, tick_ + steps, layered ? 1u : 0u};
    const DrawConstants scatter_parameters{active_effects, layered ? 1u : 0u};
    const DrawConstants both_layers{capacity, 0, capacity, 1};
    const DrawConstants smoke_layer{capacity, 0, light_first, std::max(offscreen_divisor, 1u)};
    const DrawConstants light_layer{capacity, light_first, capacity, 1};
    const DrawConstants debris_list{capacity, particle_layout::debris_list_first, capacity};
    return {
        {{{*compute_, simulation, parameters, {groups, 1, 1}, "Particle simulation"},
            {*scan_, scan, scan_parameters, {1, 1, 1}, "Particle scan"},
            {*scatter_, scatter, scatter_parameters, {groups, 1, 1}, "Particle scatter"}}},
        {*graphics_, drawing, both_layers, 0, 0, "Particles", arguments_.get(), particle_layout::particle_arguments_offset},
        {*offscreen_graphics_, drawing, smoke_layer, 0, 0, "Smoke", arguments_.get(), particle_layout::smoke_arguments_offset},
        {*graphics_, drawing, light_layer, 0, 0, "Light", arguments_.get(), particle_layout::light_arguments_offset},
        {*debris_graphics_, drawing, debris_list, 0, 0, "Debris", arguments_.get(), particle_layout::debris_arguments_offset},
        {arguments_.get(), particle_layout::counters_offset, static_cast<std::uint32_t>(sizeof(ParticleCounters))},
        steps, active_effects};
}

std::optional<ParticleCounters> ParticleSystem::counters(const ReadbackData& data)
{
    static_assert(sizeof(ParticleCounters) == particle_layout::arguments_bytes - particle_layout::counters_offset);
    if (data.size != sizeof(ParticleCounters)) return std::nullopt;
    ParticleCounters counters;
    std::memcpy(&counters, data.bytes.data(), sizeof(counters));
    return counters;
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
