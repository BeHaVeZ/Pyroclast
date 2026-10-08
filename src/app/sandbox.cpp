#include "sandbox.hpp"
#include "../simulation/impact_patterns.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

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

void Sandbox::spawn(const Ray& ray, std::string_view source)
{
    showcase.fire(range.environment(), props_shown(), ray, showcase.count, source, effects);
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

void Sandbox::update_aim(bool needed)
{
    aim.reset();
    if (needed) aim = range.environment().raycast(crosshair_ray(camera.camera()), std::numeric_limits<float>::infinity(), props_shown());
}

void Sandbox::build_marks()
{
    marks.clear();
    if (!views.spawn_regions && !views.wind_arrow) return;
    showcase_marks(views, showcase, aim, camera.camera(), range.environment(), props_shown(), marks);
}

void Sandbox::fill_snapshot(DiagnosticsSnapshot& snapshot) const
{
    const auto tick = particles.tick();
    snapshot.clock.tick = tick;
    snapshot.clock.paused = particles.paused();
    snapshot.clock.time_scale = static_cast<float>(particles.time_scale());
    snapshot.clock.capped_frames = particles.capped_frames();

    auto& load = snapshot.load;
    load.explosions = static_cast<std::uint32_t>(group.size());
    load.active_effects = static_cast<std::uint32_t>(effects.active().size());
    load.effect_slots = effects.capacity();
    load.started = effects.started();
    load.dropped = effects.dropped();
    load.dropped_since_clear = pressure.dropped_since_clear();
    load.dropped_recent = pressure.dropped_recent(tick);
    load.peak_since_clear = pressure.peak_since_clear();
    load.queued = effects.pending();
    for (const auto slot : effects.active()) ++load.active_by_preset[static_cast<std::uint32_t>(effects.slots()[slot].preset)];
    load.simulated_records = load.active_effects * ParticleSystem::slab_records;
    load.pool_capacity = ParticleSystem::capacity;
    if (!effects.active().empty()) load.particles = counters;
    load.lights = renderer.lights();
    load.coverage = scalability.coverage || scalability.smoke_resolution == SmokeResolution::automatic;
    load.smoke_resolution = smoke_resolution_name(scalability.smoke_resolution);
    load.smoke_half = renderer.half_smoke();
    load.half_light = scalability.half_light;
    load.smoke_count_budget = scalability.smoke_budget.count;
    load.smoke_limit = SmokeResolutionPolicy::limit(scalability.smoke_budget);
    if (counters)
    {
        load.smoke_load = SmokeResolutionPolicy::load(*counters, scalability.smoke_budget,
            static_cast<double>(snapshot.system.width) * static_cast<double>(snapshot.system.height));
    }
    load.smoke_switches = renderer.smoke_policy().switches();
    load.smoke_lod = scalability.smoke_lod;
    load.faint_floor = scalability.faint_floor;

    auto& shown = snapshot.showcase;
    shown.preset = showcase.preset;
    shown.count = showcase.count;
    shown.preset_ticks = effect_ticks(showcase.preset, SurfaceType::dirt);
    if (aim)
    {
        DiagnosticsSnapshot::Aim target;
        target.point = aim->point;
        target.distance = length(aim->point - camera.camera().position);
        target.surface = surface_name(aim->surface);
        if (aim->prop)
        {
            const auto& prop = range.environment().props()[*aim->prop];
            target.prop = prop.name;
            target.material = material_name(prop.material);
        }
        shown.aim = target;
    }
    if (showcase.barrage) shown.barrage_progress = showcase.barrage->next;
    if (showcase.bombardment) shown.bombardment_impacts = showcase.bombardment->released;
    shown.bombardment_interval = showcase.bombardment_interval;
    shown.bombardment_radius = showcase.bombardment_radius;

    const auto& view = camera.camera();
    auto& rig = snapshot.camera;
    rig.position[0] = view.position.x;
    rig.position[1] = view.position.y;
    rig.position[2] = view.position.z;
    rig.yaw = view.yaw * 57.2957795f;
    rig.pitch = view.pitch * 57.2957795f;
    rig.speed = camera.speed();
    rig.walking = camera.mode() == CameraMode::walk;
    for (std::size_t index = 0; index < CameraRig::bookmark_count; ++index)
    {
        rig.bookmarks[index] = camera.bookmarks()[index].name;
        rig.stored[index] = camera.bookmarks()[index].stored;
    }

    const auto now = wind_velocity(wind, tick, ((layer_mask >> particle_layout::gusts_mask_bit) & 1) != 0);
    const float3 right = view.right();
    const float3 forward{std::sin(view.yaw), 0, -std::cos(view.yaw)};
    snapshot.wind.settings = wind;
    snapshot.wind.now_speed = length(now);
    snapshot.wind.now_heading = std::atan2(now.z, now.x) * 57.2957795f;
    snapshot.wind.right = dot(now, right);
    snapshot.wind.forward = dot(now, forward);
    snapshot.wind.north_right = dot(float3{0, 0, -1}, right);
    snapshot.wind.north_forward = dot(float3{0, 0, -1}, forward);

    snapshot.views = {views.effect_bounds, views.collision_boxes, views.spawn_regions, views.wind_arrow};
    snapshot.layer_mask = layer_mask;
}

void Sandbox::release_due()
{
    showcase.release_due(particles.tick(), range.environment(), props_shown(), effects);
}

namespace
{
void log_views(const DebugViews& views)
{
    const auto state = [](bool on) { return on ? "on" : "off"; };
    SDL_Log("Debug views: effect bounds %s, collision boxes %s, spawn regions %s, wind arrow %s.", state(views.effect_bounds),
        state(views.collision_boxes), state(views.spawn_regions), state(views.wind_arrow));
}
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
    case Kind::toggle_debug_lines:
    {
        const bool on = !(sandbox.views.effect_bounds || sandbox.views.collision_boxes);
        sandbox.views.effect_bounds = on;
        sandbox.views.collision_boxes = on;
        log_views(sandbox.views);
        break;
    }
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
    case Kind::set_smoke_resolution:
        if (command.value <= 2)
        {
            sandbox.scalability.smoke_resolution = static_cast<SmokeResolution>(command.value);
            SDL_Log("Smoke resolution: %s.", smoke_resolution_name(sandbox.scalability.smoke_resolution));
        }
        break;
    case Kind::set_smoke_budget:
        if (std::isfinite(command.scalar) && command.scalar > 0)
        {
            sandbox.scalability.smoke_budget = {command.value != 0, command.scalar};
            SDL_Log("Smoke budget: %s %.0f.", command.value != 0 ? "count" : "coverage", static_cast<double>(command.scalar));
        }
        break;
    case Kind::toggle_half_light:
        sandbox.scalability.half_light = !sandbox.scalability.half_light;
        SDL_Log("Half-resolution light: %s.", sandbox.scalability.half_light ? "on" : "off");
        break;
    case Kind::set_smoke_lod:
        if (std::isfinite(command.scalar) && command.scalar >= 0 && command.scalar <= 4096)
        {
            sandbox.scalability.smoke_lod = command.scalar;
            SDL_Log("Smoke thinning above %.0f pixels (0: off).", static_cast<double>(command.scalar));
        }
        break;
    case Kind::set_faint_floor:
        if (std::isfinite(command.scalar) && command.scalar >= 0 && command.scalar < 1)
        {
            sandbox.scalability.faint_floor = command.scalar;
            SDL_Log("Faint smoke floor: %g.", static_cast<double>(command.scalar));
        }
        break;
    case Kind::toggle_coverage:
        sandbox.scalability.coverage = !sandbox.scalability.coverage;
        SDL_Log("Coverage counters: %s.", sandbox.scalability.coverage ? "on" : "off");
        break;
    case Kind::toggle_debug_view:
    {
        bool* const views[] = {&sandbox.views.effect_bounds, &sandbox.views.collision_boxes, &sandbox.views.spawn_regions,
            &sandbox.views.wind_arrow};
        if (command.value < std::size(views))
        {
            *views[command.value] = !*views[command.value];
            log_views(sandbox.views);
        }
        break;
    }
    }
}
}
