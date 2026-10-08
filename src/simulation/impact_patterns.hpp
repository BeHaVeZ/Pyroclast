#pragma once

#include "environment.hpp"
#include "explosion_system.hpp"
#include "terrain.hpp"
#include "../core/math.hpp"

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
}
