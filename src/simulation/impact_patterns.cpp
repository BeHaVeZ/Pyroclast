#include "impact_patterns.hpp"

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
}
