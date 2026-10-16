#pragma once

#include "environment.hpp"
#include "explosion_scenarios.hpp"
#include "explosion_system.hpp"
#include "terrain.hpp"
#include "../core/math.hpp"

#include <cmath>
#include <cstdint>
#include <optional>

namespace pyroclast
{
float pattern_jitter(std::uint32_t value);

ExplosionEvent ground_event(const Terrain& terrain, float x, float z, std::uint32_t seed, ExplosionPreset preset);

inline constexpr std::uint32_t barrage_impacts = 80;
inline constexpr std::uint32_t barrage_interval = 15;
float2 barrage_offset(std::uint32_t key, std::uint32_t k);

struct AimedEvent
{
    SurfaceHit hit;
    ExplosionEvent event;
};
std::optional<AimedEvent> aimed_event(const Environment& environment, const Ray& ray, std::uint32_t seed, ExplosionPreset preset,
    bool props);

std::optional<ExplosionEvent> drop_event(const Environment& environment, float x, float z, std::uint32_t seed, ExplosionPreset preset,
    bool props);

// Point 0 is the aim itself and points come nearest first.
inline const float cluster_spacing = group_spacing / std::sqrt(3.14159265f);
float2 cluster_offset(std::uint32_t key, std::uint32_t index);
float cluster_radius(std::uint32_t count);
float2 bombardment_offset(std::uint32_t key, std::uint32_t k, float radius);

// Placed by index, so the events depend only on the ticks reached, never on how ticks fell into frames.
struct ImpactSequence
{
    enum class Kind
    {
        barrage,
        bombardment,
    };
    Kind kind = Kind::barrage;
    float2 centre;
    float2 along{1, 0};
    std::uint32_t key = 0;
    ExplosionPreset preset = ExplosionPreset::small_he;
    std::uint32_t start_tick = 0;
    std::uint32_t interval = barrage_interval;
    float radius = 0;
    std::uint32_t count = 0;
    std::uint32_t next = 0;
    std::uint32_t released = 0;
    std::uint32_t off_range = 0;
};
float2 impact_position(const ImpactSequence& sequence, std::uint32_t k);
bool finished(const ImpactSequence& sequence);
// An impact off the range takes no seed.
std::uint32_t release_due(ImpactSequence& sequence, std::uint32_t tick, const Environment& environment, bool props,
    std::uint32_t& next_seed, ExplosionSystem& effects);
}
