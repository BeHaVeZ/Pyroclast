#include "sandbox.hpp"
#include "../simulation/impact_patterns.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>

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
    showcase.fire(range.environment(), props_shown(), ray, showcase.count, "cursor", effects);
}

void Sandbox::report_drops(const SlotPressure::Report& report, bool before_clear) const
{
    char ticks[48];
    if (report.first_tick == report.last_tick) std::snprintf(ticks, sizeof(ticks), "tick %u", report.last_tick);
    else std::snprintf(ticks, sizeof(ticks), "ticks %u-%u", report.first_tick, report.last_tick);
    if (before_clear)
    {
        SDL_Log("%llu events dropped at %s before the clear: all %u effect slots were busy.", static_cast<unsigned long long>(report.events),
            ticks, effects.capacity());
        return;
    }
    SDL_Log("%llu events dropped at %s: all %u effect slots are busy (%llu since clear).", static_cast<unsigned long long>(report.events),
        ticks, effects.capacity(), static_cast<unsigned long long>(pressure.dropped_since_clear()));
}

void Sandbox::release_due()
{
    showcase.release_due(particles.tick(), range.environment(), props_shown(), effects);
}

void apply(UiCommand command, Sandbox& sandbox)
{
    using Kind = UiCommand::Kind;
    switch (command.kind)
    {
    case Kind::trigger:
        sandbox.showcase.stop();
        sandbox.trigger_group();
        break;
    case Kind::clear:
        sandbox.showcase.stop();
        if (const auto report = sandbox.pressure.clear(sandbox.effects.dropped())) sandbox.report_drops(*report, true);
        sandbox.clear();
        break;
    case Kind::toggle_pause: sandbox.particles.toggle_pause(); break;
    case Kind::step: sandbox.particles.step(); break;
    case Kind::toggle_debug_lines: sandbox.debug_lines = !sandbox.debug_lines; break;
    case Kind::select_preset:
        if (command.value < preset_count)
        {
            sandbox.showcase.preset = static_cast<ExplosionPreset>(command.value);
            const auto name = preset_name(sandbox.showcase.preset);
            SDL_Log("Selected preset: %.*s", static_cast<int>(name.size()), name.data());
        }
        break;
    case Kind::toggle_layer: sandbox.layer_mask ^= 1u << (command.value & 31); break;
    case Kind::set_wind:
        if (const auto wind = validated(command.wind)) sandbox.wind = *wind;
        break;
    case Kind::toggle_slow_motion: sandbox.particles.set_time_scale(sandbox.particles.time_scale() == 1 ? 0.25 : 1); break;
    case Kind::toggle_walk:
        if (!sandbox.camera.toggle_walk(sandbox.range.environment(), sandbox.props_shown()))
        {
            SDL_Log("Camera: cannot walk here, nothing below.");
        }
        else if (sandbox.camera.mode() == CameraMode::walk)
        {
            SDL_Log("Camera: walk (%.0f m/s, eye %.1f m).", static_cast<double>(sandbox.camera.speed()), static_cast<double>(walk_eye_height));
        }
        else SDL_Log("Camera: fly (%.0f m/s).", static_cast<double>(sandbox.camera.speed()));
        break;
    case Kind::recall_bookmark:
        if (sandbox.camera.recall(command.value))
        {
            const auto& bookmark = sandbox.camera.bookmarks()[command.value];
            SDL_Log("Camera bookmark %u (%.*s) recalled: %s.", command.value + 1,
                static_cast<int>(bookmark.stored ? 6 : bookmark.name.size()), bookmark.stored ? "stored" : bookmark.name.data(),
                bookmark.mode == CameraMode::walk ? "walk" : "fly");
        }
        break;
    case Kind::store_bookmark:
        if (sandbox.camera.store(command.value))
        {
            const auto& camera = sandbox.camera.camera();
            SDL_Log("Camera bookmark %u stored: position (%.2f, %.2f, %.2f), heading %.2f deg, elevation %.2f deg, %s.",
                command.value + 1, static_cast<double>(camera.position.x), static_cast<double>(camera.position.y),
                static_cast<double>(camera.position.z), static_cast<double>(camera.yaw * 57.29578f),
                static_cast<double>(camera.pitch * 57.29578f), sandbox.camera.mode() == CameraMode::walk ? "walk" : "fly");
        }
        break;
    case Kind::set_camera_speed: sandbox.camera.set_speed(command.scalar); break;
    case Kind::set_count:
    {
        const auto& counts = Showcase::counts;
        const bool listed = std::find(counts.begin(), counts.end(), command.value) != counts.end();
        if (command.value == 0 || listed)
        {
            sandbox.showcase.count = listed ? command.value : sandbox.showcase.next_count();
            SDL_Log("Spawn count: %u", sandbox.showcase.count);
        }
        break;
    }
    case Kind::fire:
        sandbox.showcase.fire(sandbox.range.environment(), sandbox.props_shown(), crosshair_ray(sandbox.camera.camera()),
            command.value != 0 ? command.value : sandbox.showcase.count, "crosshair", sandbox.effects);
        break;
    case Kind::barrage:
        sandbox.showcase.start_barrage(sandbox.range.environment(), sandbox.props_shown(), sandbox.camera.camera(), sandbox.particles.tick());
        break;
    case Kind::toggle_bombardment:
        sandbox.showcase.toggle_bombardment(sandbox.range.environment(), sandbox.props_shown(), sandbox.camera.camera(),
            sandbox.particles.tick());
        break;
    case Kind::set_bombardment: sandbox.showcase.set_bombardment(command.value, command.scalar, sandbox.particles.tick()); break;
    }
}
}
