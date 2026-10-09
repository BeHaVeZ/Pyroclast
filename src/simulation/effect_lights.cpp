#include "effect_lights.hpp"

#include <algorithm>
#include <array>
#include <cassert>

namespace pyroclast
{
std::uint32_t select_lights(std::span<const EffectSlot> slots, std::span<const std::uint32_t> active, std::uint32_t tick,
    std::span<PointLight> lights, bool realism)
{
    const auto curve_of = [realism](ExplosionPreset preset) { return realism ? realism_light_curve(preset) : light_curve(preset); };
    struct Candidate
    {
        float intensity;
        std::uint32_t slot;
    };
    std::array<Candidate, particle_layout::maximum_effects> candidates;
    assert(active.size() <= candidates.size());
    std::uint32_t count = 0;
    for (const auto index : active)
    {
        const auto& slot = slots[index];
        const float seconds = static_cast<float>(tick - slot.start_tick) / 60.0f;
        const float intensity = light_intensity(curve_of(slot.preset), seconds);
        if (intensity > 0) candidates[count++] = {intensity, index};
    }
    std::stable_sort(candidates.begin(), candidates.begin() + count,
        [](const Candidate& a, const Candidate& b) { return a.intensity > b.intensity; });
    const auto written = std::min<std::uint32_t>(count, static_cast<std::uint32_t>(lights.size()));
    for (std::uint32_t index = 0; index < written; ++index)
    {
        const auto& slot = slots[candidates[index].slot];
        const auto curve = curve_of(slot.preset);
        lights[index] = {slot.origin + slot.normal * curve.height, curve.radius, curve.color * candidates[index].intensity, curve.source_radius};
    }
    return written;
}
}
