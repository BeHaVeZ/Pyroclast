#include "blast_front.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace pyroclast
{
namespace
{
constexpr double sound_speed = 340.0;
constexpr double gamma_air = 1.4;
constexpr double shock_factor = (gamma_air + 1) / (2 * gamma_air);
constexpr double ticks_per_second = 60.0;
constexpr double last_knot_metres = 600.0;
constexpr double q16 = 65536.0;
constexpr double q24 = 16777216.0;

double ticks_per_log_radius(double radius, double scale)
{
    return radius / front_speed(overpressure_ratio(radius / scale)) * ticks_per_second;
}

double ticks_between(double from, double to, double scale, int steps = 32)
{
    const double span = std::log(to / from);
    const double step = span / steps;
    double sum = ticks_per_log_radius(from, scale) + ticks_per_log_radius(to, scale);
    for (int index = 1; index < steps; ++index)
    {
        sum += (index % 2 == 1 ? 4.0 : 2.0) * ticks_per_log_radius(from * std::exp(step * index), scale);
    }
    return sum * step / 3.0;
}

template <typename Key>
std::size_t segment(const BlastParameters& blast, Key key)
{
    std::size_t index = 0;
    while (index + 2 < blast.knots.size() && key(blast.knots[index + 1])) ++index;
    return index;
}
}

double overpressure_ratio(double scaled_distance)
{
    const double z = std::max(scaled_distance, 1e-3);
    const double a = z / 4.5;
    const double b = z / 0.048;
    const double c = z / 0.32;
    const double d = z / 1.35;
    return 808.0 * (1 + a * a) / std::sqrt((1 + b * b) * (1 + c * c) * (1 + d * d));
}

double front_speed(double overpressure)
{
    return sound_speed * std::sqrt(1 + shock_factor * overpressure);
}

double particle_velocity(double overpressure)
{
    return sound_speed / gamma_air * overpressure / std::sqrt(1 + shock_factor * overpressure);
}

std::array<BlastKnot, realism_layout::blast_knots> make_blast_knots(double kilograms)
{
    const double scale = std::cbrt(kilograms);
    const double first = scale;
    const double ratio = std::pow(last_knot_metres / first, 1.0 / (realism_layout::blast_knots - 1));
    std::array<BlastKnot, realism_layout::blast_knots> knots{};
    double ticks = 0;
    double previous = first;
    for (std::uint32_t index = 0; index < knots.size(); ++index)
    {
        const double radius = index + 1 == knots.size() ? last_knot_metres : first * std::pow(ratio, index);
        if (index > 0) ticks += ticks_between(previous, radius, scale);
        previous = radius;
        knots[index].radius_cm = static_cast<std::uint32_t>(std::llround(radius * 100));
        knots[index].arrival_q16 = static_cast<std::uint32_t>(std::llround(ticks * q16));
        knots[index].overpressure = static_cast<float>(overpressure_ratio(radius / scale));
    }
    for (std::uint32_t index = 0; index + 1 < knots.size(); ++index)
    {
        const double ticks_q16 = static_cast<double>(knots[index + 1].arrival_q16 - knots[index].arrival_q16);
        const double centimetres = static_cast<double>(knots[index + 1].radius_cm - knots[index].radius_cm);
        knots[index].ticks_per_cm_q24 = static_cast<std::uint32_t>(std::llround(ticks_q16 / q16 * q24 / centimetres));
    }
    knots.back().ticks_per_cm_q24 = knots[knots.size() - 2].ticks_per_cm_q24;
    return knots;
}

double integrated_arrival_ticks(double kilograms, double metres)
{
    const double scale = std::cbrt(kilograms);
    return metres <= scale ? 0.0 : ticks_between(scale, metres, scale, 4096);
}

float blast_radius(const BlastParameters& blast, float ticks)
{
    const float time_q16 = std::max(ticks, 0.0f) * 65536.0f;
    const auto index = segment(blast, [time_q16](const BlastKnot& knot) { return static_cast<float>(knot.arrival_q16) <= time_q16; });
    const auto& knot = blast.knots[index];
    const float centimetres = static_cast<float>(knot.radius_cm) +
        (time_q16 - static_cast<float>(knot.arrival_q16)) * 256.0f / static_cast<float>(knot.ticks_per_cm_q24);
    return centimetres / 100.0f;
}

float blast_overpressure(const BlastParameters& blast, float metres)
{
    const float centimetres = metres * 100.0f;
    const auto whole = static_cast<std::uint32_t>(std::max(centimetres, 0.0f));
    const auto index = segment(blast, [whole](const BlastKnot& knot) { return knot.radius_cm <= whole; });
    const auto& near = blast.knots[index];
    const auto& far = blast.knots[index + 1];
    const float near_radius = static_cast<float>(near.radius_cm);
    const float exponent = std::log2(far.overpressure / near.overpressure) / std::log2(static_cast<float>(far.radius_cm) / near_radius);
    const float clamped = std::max(centimetres, static_cast<float>(blast.knots[0].radius_cm));
    return near.overpressure * std::exp2(exponent * std::log2(clamped / near_radius));
}

std::uint32_t blast_arrival_tick(const BlastParameters& blast, std::uint32_t radius_cm)
{
    if (radius_cm <= blast.knots[0].radius_cm) return 0;
    const auto index = segment(blast, [radius_cm](const BlastKnot& knot) { return knot.radius_cm <= radius_cm; });
    const auto& knot = blast.knots[index];
    const std::uint32_t arrival_q16 = knot.arrival_q16 + (((radius_cm - knot.radius_cm) * knot.ticks_per_cm_q24) >> 8);
    return (arrival_q16 + 0xffffu) >> 16;
}

float wilson_opacity(const BlastParameters& blast, float ticks, float humidity)
{
    const float first = blast.wilson.y;
    const float last = blast.wilson.z;
    if (blast.wilson.x <= 0 || ticks <= first || ticks >= last) return 0;
    const float window = std::sin(std::numbers::pi_v<float> * (ticks - first) / (last - first));
    return blast.wilson.x * window * std::clamp((humidity - 0.7f) / 0.2f, 0.0f, 1.0f);
}

float push_speed(const BlastParameters& blast, float metres)
{
    return std::min(blast.push_share * static_cast<float>(particle_velocity(blast_overpressure(blast, metres))), blast.push_cap);
}

float blast_arrival_ticks(const BlastParameters& blast, float metres)
{
    const float centimetres = metres * 100.0f;
    if (centimetres <= static_cast<float>(blast.knots[0].radius_cm)) return 0;
    const auto whole = static_cast<std::uint32_t>(centimetres);
    const auto index = segment(blast, [whole](const BlastKnot& knot) { return knot.radius_cm <= whole; });
    const auto& knot = blast.knots[index];
    const float arrival_q16 = static_cast<float>(knot.arrival_q16) +
        (centimetres - static_cast<float>(knot.radius_cm)) * static_cast<float>(knot.ticks_per_cm_q24) / 256.0f;
    return arrival_q16 / 65536.0f;
}

float shake_angle(const ExplosionSystem& effects, std::span<const BlastParameters> classes, float3 camera, std::uint32_t tick)
{
    constexpr float per_overpressure = 0.05f;
    constexpr float largest = 0.02f;
    constexpr float decay_seconds = 0.25f;
    constexpr float frequency = 9.0f;
    constexpr float window_seconds = 1.5f;
    float angle = 0;
    const auto slots = effects.slots();
    for (const auto index : effects.active())
    {
        const auto& slot = slots[index];
        if (slot.effect_class >= classes.size() || tick < slot.start_tick) continue;
        const auto& blast = classes[slot.effect_class];
        const float3 offset = camera - slot.origin;
        const float distance = std::sqrt(dot(offset, offset));
        const float since = (static_cast<float>(tick - slot.start_tick) - blast_arrival_ticks(blast, distance)) / 60.0f;
        if (since < 0 || since > window_seconds) continue;
        const float amplitude = std::min(per_overpressure * blast_overpressure(blast, distance), largest);
        angle += amplitude * std::exp(-since / decay_seconds) * std::sin(2 * std::numbers::pi_v<float> * frequency * since);
    }
    return std::clamp(angle, -largest, largest);
}

BlastList select_blasts(const ExplosionSystem& effects, std::span<const BlastParameters> classes, std::uint32_t tick_before, std::uint32_t tick_drawn,
    float humidity)
{
    std::array<std::uint32_t, realism_layout::maximum_blasts + 1> chosen{};
    std::uint32_t count = 0;
    const auto slots = effects.slots();
    const auto newer = [&slots](std::uint32_t a, std::uint32_t b) {
        return slots[a].start_tick != slots[b].start_tick ? slots[a].start_tick > slots[b].start_tick : a > b;
    };
    for (const auto index : effects.active())
    {
        const auto& slot = slots[index];
        if (slot.effect_class >= classes.size() || tick_drawn < slot.start_tick) continue;
        if (tick_drawn - slot.start_tick >= classes[slot.effect_class].wave_ticks) continue;
        std::uint32_t at = count;
        while (at > 0 && newer(index, chosen[at - 1]))
        {
            chosen[at] = chosen[at - 1];
            --at;
        }
        chosen[at] = index;
        count = std::min<std::uint32_t>(count + 1, realism_layout::maximum_blasts);
    }
    BlastList list;
    list.count = count;
    for (std::uint32_t entry = 0; entry < count; ++entry)
    {
        const auto& slot = slots[chosen[entry]];
        const auto& blast = classes[slot.effect_class];
        const float ticks = static_cast<float>(tick_drawn - slot.start_tick);
        const float radius = blast_radius(blast, ticks);
        const float before = tick_before < slot.start_tick ? 0.0f : blast_radius(blast, static_cast<float>(tick_before - slot.start_tick));
        list.slots[entry] = chosen[entry];
        list.rings[entry] = {radius, blast.wave.x * blast_overpressure(blast, radius), blast.wave.y + blast.wave.z * radius,
            wilson_opacity(blast, ticks, humidity)};
        const float shell = blast.wilson.x > 0 ? std::min(0.85f * radius, blast.wilson.w) : 0.0f;
        list.fronts[entry] = {before, push_speed(blast, std::max(before, 0.01f)), push_speed(blast, radius), shell};
    }
    return list;
}
}
