#include "impact_patterns.hpp"

#include <cmath>
#include <limits>

namespace pyroclast
{
float pattern_jitter(std::uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return static_cast<float>(value & 0xffffu) / 32768.0f - 1.0f;
}

ExplosionEvent ground_event(const Terrain& terrain, float x, float z, std::uint32_t seed, ExplosionPreset preset)
{
    const auto normal = terrain.normal(x, z);
    return {float3{x, terrain.height(x, z), z} + normal * 0.5f, normal, seed, preset, Terrain::surface(x, z)};
}

float2 barrage_offset(std::uint32_t key, std::uint32_t k)
{
    return {-60.0f + 120.0f * static_cast<float>(k) / static_cast<float>(barrage_impacts - 1) + 4.0f * pattern_jitter(key + 2 * k),
        12.0f * pattern_jitter(key + 2 * k + 1)};
}

std::optional<AimedEvent> aimed_event(const Environment& environment, const Ray& ray, std::uint32_t seed, ExplosionPreset preset,
    bool props)
{
    const auto hit = environment.raycast(ray, std::numeric_limits<float>::infinity(), props);
    if (!hit) return std::nullopt;
    const auto origin = props ? environment.event_origin(*hit) : hit->point + hit->normal * 0.5f;
    return AimedEvent{*hit, {origin, hit->normal, seed, preset, hit->surface}};
}

std::optional<ExplosionEvent> drop_event(const Environment& environment, float x, float z, std::uint32_t seed, ExplosionPreset preset,
    bool props)
{
    if (!(std::abs(x) <= Terrain::half_extent && std::abs(z) <= Terrain::half_extent)) return std::nullopt;
    if (props)
    {
        const auto hit = environment.raycast({{x, 1000, z}, {0, -1, 0}}, std::numeric_limits<float>::infinity(), true);
        if (hit && hit->prop) return ExplosionEvent{environment.event_origin(*hit), hit->normal, seed, preset, hit->surface};
    }
    return ground_event(environment.terrain(), x, z, seed, preset);
}

float2 cluster_offset(std::uint32_t key, std::uint32_t index)
{
    if (index == 0) return {0, 0};
    constexpr float golden_angle = 2.39996323f;
    const float radius = cluster_spacing * std::sqrt(static_cast<float>(index));
    const float angle = golden_angle * static_cast<float>(index);
    const float jitter = 0.15f * cluster_spacing;
    return {radius * std::cos(angle) + jitter * pattern_jitter(key + 2 * index),
        radius * std::sin(angle) + jitter * pattern_jitter(key + 2 * index + 1)};
}

float cluster_radius(std::uint32_t count)
{
    if (count <= 1) return 0;
    return cluster_spacing * (std::sqrt(static_cast<float>(count - 1)) + 0.15f * std::sqrt(2.0f));
}

float2 bombardment_offset(std::uint32_t key, std::uint32_t k, float radius)
{
    const float u = 0.5f * (pattern_jitter(key + 2 * k) + 1);
    const float v = 0.5f * (pattern_jitter(key + 2 * k + 1) + 1);
    const float distance = radius * std::sqrt(u);
    const float angle = 6.28318531f * v;
    return {distance * std::cos(angle), distance * std::sin(angle)};
}

float2 impact_position(const ImpactSequence& sequence, std::uint32_t k)
{
    if (sequence.kind == ImpactSequence::Kind::bombardment)
    {
        const auto offset = bombardment_offset(sequence.key, k, sequence.radius);
        return {sequence.centre.x + offset.x, sequence.centre.y + offset.y};
    }
    const auto offset = barrage_offset(sequence.key, k);
    const float2 across{-sequence.along.y, sequence.along.x};
    return {sequence.centre.x + sequence.along.x * offset.x + across.x * offset.y,
        sequence.centre.y + sequence.along.y * offset.x + across.y * offset.y};
}

bool finished(const ImpactSequence& sequence)
{
    return sequence.count != 0 && sequence.next >= sequence.count;
}

std::uint32_t release_due(ImpactSequence& sequence, std::uint32_t tick, const Environment& environment, bool props,
    std::uint32_t& next_seed, ExplosionSystem& effects)
{
    std::uint32_t released = 0;
    while (!finished(sequence) && sequence.start_tick + sequence.next * sequence.interval <= tick)
    {
        const auto position = impact_position(sequence, sequence.next++);
        const auto event = drop_event(environment, position.x, position.y, next_seed, sequence.preset, props);
        if (!event)
        {
            ++sequence.off_range;
            continue;
        }
        ++next_seed;
        effects.submit(*event);
        ++sequence.released;
        ++released;
    }
    return released;
}
}
