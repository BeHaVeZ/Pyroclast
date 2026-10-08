#include "showcase.hpp"
#include "camera_controls.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pyroclast
{
namespace
{
double milliseconds_since(std::uint64_t start)
{
    return 1000.0 * static_cast<double>(SDL_GetPerformanceCounter() - start) / static_cast<double>(SDL_GetPerformanceFrequency());
}

std::optional<SurfaceHit> crosshair_hit(const Environment& environment, bool props, const Camera& camera)
{
    const auto hit = environment.raycast(crosshair_ray(camera), std::numeric_limits<float>::infinity(), props);
    if (!hit) SDL_Log("Nothing under the crosshair.");
    return hit;
}

int length(std::string_view text)
{
    return static_cast<int>(text.size());
}
}

std::optional<Showcase::Fired> Showcase::fire(const Environment& environment, bool props, const Ray& ray, std::uint32_t events,
    std::string_view source, ExplosionSystem& effects)
{
    const auto start = SDL_GetPerformanceCounter();
    const auto aimed = aimed_event(environment, ray, next_seed, preset, props);
    if (!aimed)
    {
        SDL_Log("Nothing under the %.*s.", length(source), source.data());
        return std::nullopt;
    }
    const auto first = next_seed;
    Fired fired;
    effects.submit(aimed->event);
    ++next_seed;
    ++fired.queued;
    const auto& hit = aimed->hit;
    for (std::uint32_t index = 1; index < events; ++index)
    {
        const auto offset = cluster_offset(2 * first, index);
        const auto event = drop_event(environment, hit.point.x + offset.x, hit.point.z + offset.y, next_seed, preset, props);
        if (!event)
        {
            ++fired.off_range;
            continue;
        }
        effects.submit(*event);
        ++next_seed;
        ++fired.queued;
    }
    const auto name = preset_name(preset);
    if (events > 1)
    {
        SDL_Log("Spawned %u %.*s around (%.1f, %.1f, %.1f): %u queued, %u off the range; seeds %u-%u; %.2f ms.", events, length(name),
            name.data(), static_cast<double>(hit.point.x), static_cast<double>(hit.point.y), static_cast<double>(hit.point.z), fired.queued,
            fired.off_range, first, next_seed - 1, milliseconds_since(start));
        return fired;
    }
    const auto surface = surface_name(hit.surface);
    if (hit.prop)
    {
        const auto& prop = environment.props()[*hit.prop];
        const auto material = material_name(prop.material);
        SDL_Log("Explosion at (%.1f, %.1f, %.1f): %.*s on the %.*s (%.*s: %.*s response).", static_cast<double>(hit.point.x),
            static_cast<double>(hit.point.y), static_cast<double>(hit.point.z), length(name), name.data(), length(prop.name),
            prop.name.data(), length(material), material.data(), length(surface), surface.data());
        return fired;
    }
    SDL_Log("Explosion at (%.1f, %.1f, %.1f): %.*s on %.*s.", static_cast<double>(hit.point.x), static_cast<double>(hit.point.y),
        static_cast<double>(hit.point.z), length(name), name.data(), length(surface), surface.data());
    return fired;
}

bool Showcase::start_barrage(const Environment& environment, bool props, const Camera& camera, std::uint32_t tick)
{
    const auto hit = crosshair_hit(environment, props, camera);
    if (!hit) return false;
    if (barrage)
    {
        SDL_Log("Barrage stopped after %u of %u impacts.", barrage->next, barrage->count);
    }
    const auto right = camera.right();
    ImpactSequence sequence;
    sequence.kind = ImpactSequence::Kind::barrage;
    sequence.centre = {hit->point.x, hit->point.z};
    sequence.along = {right.x, right.z};
    sequence.key = 2 * next_seed;
    sequence.preset = preset;
    sequence.start_tick = tick;
    sequence.interval = barrage_interval;
    sequence.count = barrage_impacts;
    barrage = sequence;
    const auto name = preset_name(preset);
    SDL_Log("Barrage started at (%.1f, %.1f) towards %.0f deg: %u %.*s impacts, one every %u ticks; key %u.",
        static_cast<double>(sequence.centre.x), static_cast<double>(sequence.centre.y),
        static_cast<double>(std::atan2(sequence.along.y, sequence.along.x) * 57.2957795f), barrage_impacts, length(name), name.data(),
        barrage_interval, sequence.key);
    return true;
}

bool Showcase::toggle_bombardment(const Environment& environment, bool props, const Camera& camera, std::uint32_t tick)
{
    if (bombardment)
    {
        SDL_Log("Bombardment stopped after %u impacts, %u off the range.", bombardment->released, bombardment->off_range);
        bombardment.reset();
        return true;
    }
    const auto hit = crosshair_hit(environment, props, camera);
    if (!hit) return false;
    ImpactSequence sequence;
    sequence.kind = ImpactSequence::Kind::bombardment;
    sequence.centre = {hit->point.x, hit->point.z};
    sequence.key = 2 * next_seed;
    sequence.preset = preset;
    sequence.start_tick = tick;
    sequence.interval = bombardment_interval;
    sequence.radius = bombardment_radius;
    bombardment = sequence;
    const auto name = preset_name(preset);
    SDL_Log("Bombardment started at (%.1f, %.1f): %.*s every %u ticks within %.0f m until stopped (V); key %u.",
        static_cast<double>(sequence.centre.x), static_cast<double>(sequence.centre.y), length(name), name.data(), sequence.interval,
        static_cast<double>(sequence.radius), sequence.key);
    return true;
}

void Showcase::set_bombardment(std::uint32_t interval, float radius, std::uint32_t tick)
{
    if (std::find(bombardment_intervals.begin(), bombardment_intervals.end(), interval) != bombardment_intervals.end())
    {
        bombardment_interval = interval;
    }
    if (std::isfinite(radius)) bombardment_radius = std::clamp(radius, bombardment_radius_min, bombardment_radius_max);
    if (!bombardment) return;
    bombardment->key = 2 * next_seed;
    bombardment->start_tick = tick;
    bombardment->interval = bombardment_interval;
    bombardment->radius = bombardment_radius;
    bombardment->next = 0;
    SDL_Log("Bombardment now every %u ticks within %.0f m.", bombardment_interval, static_cast<double>(bombardment_radius));
}

std::uint32_t Showcase::release_due(std::uint32_t tick, const Environment& environment, bool props, ExplosionSystem& effects)
{
    std::uint32_t released = 0;
    if (barrage)
    {
        released += pyroclast::release_due(*barrage, tick, environment, props, next_seed, effects);
        if (finished(*barrage))
        {
            SDL_Log("Barrage finished: %u impacts, %u off the range.", barrage->released, barrage->off_range);
            barrage.reset();
        }
    }
    if (bombardment) released += pyroclast::release_due(*bombardment, tick, environment, props, next_seed, effects);
    return released;
}

void Showcase::stop()
{
    if (barrage)
    {
        SDL_Log("Barrage stopped after %u of %u impacts.", barrage->next, barrage->count);
        barrage.reset();
    }
    if (bombardment)
    {
        SDL_Log("Bombardment stopped after %u impacts, %u off the range.", bombardment->released, bombardment->off_range);
        bombardment.reset();
    }
}

std::uint32_t Showcase::next_count() const
{
    const auto current = std::find(counts.begin(), counts.end(), count);
    return current == counts.end() || current + 1 == counts.end() ? counts.front() : *(current + 1);
}

std::optional<SlotPressure::Report> SlotPressure::record(std::uint32_t tick, double wall_seconds, std::uint32_t active,
    std::uint64_t dropped)
{
    const auto fresh = dropped - std::min(dropped, dropped_);
    dropped_ = std::max(dropped_, dropped);
    peak_ = std::max(peak_, active);
    if (fresh != 0)
    {
        const auto bucket = tick % recent_ticks;
        if (bucket_ticks_[bucket] != tick || buckets_[bucket] == 0)
        {
            buckets_[bucket] = 0;
            bucket_ticks_[bucket] = tick;
        }
        buckets_[bucket] += fresh;
        if (unreported_.events == 0) unreported_.first_tick = tick;
        unreported_.last_tick = tick;
        unreported_.events += fresh;
    }
    if (unreported_.events == 0 || wall_seconds - last_report_ < 1.0) return std::nullopt;
    const auto report = unreported_;
    unreported_ = {};
    last_report_ = wall_seconds;
    return report;
}

std::optional<SlotPressure::Report> SlotPressure::clear(std::uint64_t dropped)
{
    const auto pending = unreported_;
    dropped_ = std::max(dropped_, dropped);
    cleared_ = dropped_;
    peak_ = 0;
    buckets_ = {};
    bucket_ticks_ = {};
    unreported_ = {};
    if (pending.events == 0) return std::nullopt;
    return pending;
}

std::uint64_t SlotPressure::dropped_recent(std::uint32_t tick) const
{
    std::uint64_t total = 0;
    for (std::size_t index = 0; index < recent_ticks; ++index)
    {
        if (buckets_[index] != 0 && bucket_ticks_[index] <= tick && tick - bucket_ticks_[index] < recent_ticks) total += buckets_[index];
    }
    return total;
}
}
