#include "sandbox.hpp"
#include "../simulation/impact_patterns.hpp"

#include <SDL3/SDL.h>

namespace pyroclast
{
std::optional<ParticleCounters> Sandbox::poll_counters(const GraphicsDevice& device)
{
    const auto readback = device.completed_readback();
    if (!readback || readback->sample_id == counter_sample) return std::nullopt;
    counter_sample = readback->sample_id;
    const auto fresh = ParticleSystem::counters(*readback);
    if (fresh) counters = fresh;
    return fresh;
}

void Sandbox::start_group()
{
    effects.clear();
    for (const auto& event : group) effects.submit(event);
    particles.discard_pending();
}

void Sandbox::trigger_group()
{
    start_group();
    SDL_Log("Explosion group triggered: %zu effects, %zu particles; replaces the previous group.", group.size(),
        group.size() * ParticleSystem::slab_records);
}

void Sandbox::clear()
{
    effects.clear();
    particles.discard_pending();
    SDL_Log("Particle pool cleared.");
}

void Sandbox::spawn(const Ray& ray)
{
    const auto& environment = range.environment();
    const auto aimed = aimed_event(environment, ray, next_seed, click_preset, props_shown());
    if (!aimed)
    {
        SDL_Log("Nothing under the cursor.");
        return;
    }
    ++next_seed;
    effects.submit(aimed->event);
    const auto& hit = aimed->hit;
    const auto preset = preset_name(click_preset);
    const auto surface = surface_name(hit.surface);
    if (hit.prop)
    {
        const auto& prop = environment.props()[*hit.prop];
        const auto material = material_name(prop.material);
        SDL_Log("Explosion at (%.1f, %.1f, %.1f): %.*s on the %.*s (%.*s: %.*s response).", hit.point.x, hit.point.y,
            hit.point.z, static_cast<int>(preset.size()), preset.data(), static_cast<int>(prop.name.size()), prop.name.data(),
            static_cast<int>(material.size()), material.data(), static_cast<int>(surface.size()), surface.data());
        return;
    }
    SDL_Log("Explosion at (%.1f, %.1f, %.1f): %.*s on %.*s.", hit.point.x, hit.point.y, hit.point.z,
        static_cast<int>(preset.size()), preset.data(), static_cast<int>(surface.size()), surface.data());
}

void apply(UiCommand command, Sandbox& sandbox)
{
    using Kind = UiCommand::Kind;
    switch (command.kind)
    {
    case Kind::trigger: sandbox.trigger_group(); break;
    case Kind::clear: sandbox.clear(); break;
    case Kind::toggle_pause: sandbox.particles.toggle_pause(); break;
    case Kind::step: sandbox.particles.step(); break;
    case Kind::toggle_debug_lines: sandbox.debug_lines = !sandbox.debug_lines; break;
    case Kind::select_preset:
        if (command.value < preset_count) sandbox.click_preset = static_cast<ExplosionPreset>(command.value);
        break;
    case Kind::toggle_layer: sandbox.layer_mask ^= 1u << (command.value & 31); break;
    case Kind::set_wind:
        if (const auto wind = validated(command.wind)) sandbox.wind = *wind;
        break;
    case Kind::toggle_slow_motion: sandbox.particles.set_time_scale(sandbox.particles.time_scale() == 1 ? 0.25 : 1); break;
    }
}
}
