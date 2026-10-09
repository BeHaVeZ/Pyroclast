#include "app/prop_mesh.hpp"
#include "simulation/environment.hpp"
#include "simulation/explosion_scenarios.hpp"
#include "simulation/wind.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

namespace
{
using namespace pyroclast;

int failures = 0;

void check(bool value, const char* name)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

bool approx(float a, float b, float tolerance = 1e-3f)
{
    return std::abs(a - b) <= tolerance;
}

float top(const PropBox& box) { return box.centre.y + box.half_extent.y; }
float bottom(const PropBox& box) { return box.centre.y - box.half_extent.y; }

float footprint_distance(const PropBox& box, float x, float z)
{
    const auto local = to_box(box, float3{x - box.centre.x, 0, z - box.centre.z});
    const float dx = std::max(std::abs(local.x) - box.half_extent.x, 0.0f);
    const float dz = std::max(std::abs(local.z) - box.half_extent.z, 0.0f);
    return std::sqrt(dx * dx + dz * dz);
}

std::vector<float2> corners(const PropBox& box)
{
    std::vector<float2> result;
    for (const float sx : {-1.0f, 1.0f})
    {
        for (const float sz : {-1.0f, 1.0f})
        {
            const auto world = box.centre + from_box(box, {sx * box.half_extent.x, 0, sz * box.half_extent.z});
            result.push_back({world.x, world.z});
        }
    }
    return result;
}

bool footprints_overlap(const PropBox& a, const PropBox& b, float margin)
{
    const float2 axes[] = {{a.axis_cos, a.axis_sin}, {-a.axis_sin, a.axis_cos}, {b.axis_cos, b.axis_sin}, {-b.axis_sin, b.axis_cos}};
    for (const auto& axis : axes)
    {
        const auto extent = [&axis](const PropBox& box) {
            return box.half_extent.x * std::abs(box.axis_cos * axis.x + box.axis_sin * axis.y) +
                box.half_extent.z * std::abs(-box.axis_sin * axis.x + box.axis_cos * axis.y);
        };
        const float distance = std::abs((b.centre.x - a.centre.x) * axis.x + (b.centre.z - a.centre.z) * axis.y);
        if (distance >= extent(a) + extent(b) - margin) return false;
    }
    return true;
}

bool boxes_overlap(const PropBox& a, const PropBox& b, float margin)
{
    return std::min(top(a), top(b)) - std::max(bottom(a), bottom(b)) > margin && footprints_overlap(a, b, margin);
}

std::uint32_t noise_hash(int x, int y, int z, std::uint32_t salt)
{
    std::uint32_t value = static_cast<std::uint32_t>(x) * 73856093u ^ static_cast<std::uint32_t>(y) * 19349663u ^
        static_cast<std::uint32_t>(z) * 83492791u ^ salt * 2654435761u;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

float3 noise_gradient(int x, int y, int z, std::uint32_t salt)
{
    const auto value = noise_hash(x, y, z, salt);
    const auto component = [](std::uint32_t bits) { return static_cast<float>(bits & 1023u) * (2.0f / 1023.0f) - 1.0f; };
    return {component(value), component(value >> 10), component(value >> 20)};
}

float3 noise_derivatives(float3 position, std::uint32_t salt)
{
    const float3 cell{std::floor(position.x), std::floor(position.y), std::floor(position.z)};
    const int cx = static_cast<int>(cell.x), cy = static_cast<int>(cell.y), cz = static_cast<int>(cell.z);
    const float3 w = position - cell;
    const auto quintic = [](float t) { return t * t * t * (t * (t * 6 - 15) + 10); };
    const auto quintic_derivative = [](float t) { return 30 * t * t * (t * (t - 2) + 1); };
    const float3 u{quintic(w.x), quintic(w.y), quintic(w.z)};
    const float3 du{quintic_derivative(w.x), quintic_derivative(w.y), quintic_derivative(w.z)};
    const auto ga = noise_gradient(cx, cy, cz, salt), gb = noise_gradient(cx + 1, cy, cz, salt);
    const auto gc = noise_gradient(cx, cy + 1, cz, salt), gd = noise_gradient(cx + 1, cy + 1, cz, salt);
    const auto ge = noise_gradient(cx, cy, cz + 1, salt), gf = noise_gradient(cx + 1, cy, cz + 1, salt);
    const auto gg = noise_gradient(cx, cy + 1, cz + 1, salt), gh = noise_gradient(cx + 1, cy + 1, cz + 1, salt);
    const float va = dot(ga, w), vb = dot(gb, w - float3{1, 0, 0}), vc = dot(gc, w - float3{0, 1, 0});
    const float vd = dot(gd, w - float3{1, 1, 0}), ve = dot(ge, w - float3{0, 0, 1}), vf = dot(gf, w - float3{1, 0, 1});
    const float vg = dot(gg, w - float3{0, 1, 1}), vh = dot(gh, w - float3{1, 1, 1});
    const float k1 = va - vb - vc + vd, k2 = va - vc - ve + vg, k3 = va - vb - ve + vf;
    const float k4 = -va + vb + vc - vd + ve - vf - vg + vh;
    const float3 interpolated = ga + u.x * (gb - ga) + u.y * (gc - ga) + u.z * (ge - ga) + u.x * u.y * (ga - gb - gc + gd) +
        u.y * u.z * (ga - gc - ge + gg) + u.z * u.x * (ga - gb - ge + gf) + u.x * u.y * u.z * (gb + gc + ge + gh - ga - gd - gf - gg);
    const float3 values{vb - va + u.y * k1 + u.z * k3 + u.y * u.z * k4, vc - va + u.z * k2 + u.x * k1 + u.z * u.x * k4,
        ve - va + u.x * k3 + u.y * k2 + u.x * u.y * k4};
    return interpolated + float3{du.x * values.x, du.y * values.y, du.z * values.z};
}

float curl_magnitude(float3 position)
{
    const auto d1 = noise_derivatives(position, 1), d2 = noise_derivatives(position, 2), d3 = noise_derivatives(position, 3);
    return length(float3{d3.y - d2.z, d1.z - d3.x, d2.x - d1.y});
}

std::uint32_t prop_index(const Environment& environment, std::string_view name)
{
    for (std::uint32_t index = 0; index < environment.props().size(); ++index)
    {
        if (environment.props()[index].name == name) return index;
    }
    return std::numeric_limits<std::uint32_t>::max();
}

std::uint64_t fingerprint(const std::vector<ExplosionEvent>& events, const std::vector<std::uint32_t>& ticks)
{
    std::uint64_t hash = 0xcbf29ce484222325ull;
    const auto mix = [&hash](const void* data, std::size_t size) {
        for (std::size_t index = 0; index < size; ++index)
        {
            hash ^= static_cast<const unsigned char*>(data)[index];
            hash *= 0x100000001b3ull;
        }
    };
    for (std::size_t index = 0; index < events.size(); ++index)
    {
        const auto& event = events[index];
        const float values[] = {event.position.x, event.position.y, event.position.z, event.normal.x, event.normal.y, event.normal.z};
        const std::uint32_t words[] = {event.seed, static_cast<std::uint32_t>(event.preset), static_cast<std::uint32_t>(event.surface),
            index < ticks.size() ? ticks[index] : 0};
        mix(values, sizeof(values));
        mix(words, sizeof(words));
    }
    return hash;
}
}

int main()
{
    namespace layout = terrain_layout;
    const Environment environment;
    const auto bytes = environment.bytes();
    const auto boxes = environment.boxes();
    const auto props = environment.props();

    static_assert(layout::environment_grid_offset == 264208 && layout::environment_boxes_offset == 280592 &&
        layout::environment_flags_offset == 282640 && layout::environment_bytes == 282656);
    static_assert(layout::environment_grid_offset % 16 == 0 && layout::environment_boxes_offset % 16 == 0 &&
        layout::environment_flags_offset % 16 == 0 && layout::environment_bytes % 4 == 0);
    static_assert(layout::environment_grid_offset >= Terrain::resolution * Terrain::resolution * sizeof(float));
    check(bytes.size() == layout::environment_bytes, "the buffer is environment_bytes long");
    const auto heights = std::as_bytes(environment.terrain().heights());
    check(std::memcmp(bytes.data(), heights.data(), heights.size()) == 0, "the heights come first, as Terrain lays them out");
    bool padding = true;
    for (std::size_t index = heights.size(); index < layout::environment_grid_offset; ++index) padding &= bytes[index] == std::byte{0};
    check(padding, "the padding before the grid is zero");

    check(props.size() == 18 && boxes.size() == 36 && environment.box_count() == 36 && boxes.size() <= layout::maximum_prop_boxes,
        "18 props of 36 boxes, within the 64 the masks hold");
    bool decoded = true;
    bool unit_axes = true;
    bool sized = true;
    bool owned = true;
    std::uint32_t open = 0;
    for (std::uint32_t index = 0; index < boxes.size(); ++index)
    {
        const auto& box = boxes[index];
        const auto packed = environment.packed_box(index);
        decoded &= std::memcmp(&packed.centre, &box.centre, sizeof(float3)) == 0 &&
            std::memcmp(&packed.half_extent, &box.half_extent, sizeof(float3)) == 0 && packed.axis_cos == box.axis_cos &&
            packed.axis_sin == box.axis_sin && packed.open_bottom == box.open_bottom;
        unit_axes &= approx(box.axis_cos * box.axis_cos + box.axis_sin * box.axis_sin, 1, 1e-6f);
        sized &= box.half_extent.x >= 0.1f && box.half_extent.y >= 0.1f && box.half_extent.z >= 0.1f;
        const auto& prop = props[box.prop];
        owned &= index >= prop.first_box && index < prop.first_box + prop.box_count;
        if (box.open_bottom) open += 1;
    }
    check(decoded, "every box record decodes to its box");
    check(unit_axes && sized, "unit axes, half extents of at least 0.1 m");
    check(owned, "every box lies in its prop's range");
    bool unused_zero = true;
    for (std::size_t offset = layout::environment_boxes_offset + boxes.size() * layout::prop_box_stride; offset < layout::environment_flags_offset; ++offset)
    {
        unused_zero &= bytes[offset] == std::byte{0};
    }
    check(unused_zero, "unused box records are zero");
    const auto hut = prop_index(environment, "hut");
    check(open == 2 && hut < props.size() && boxes[props[hut].first_box + 5].open_bottom && boxes[props[hut].first_box + 6].open_bottom,
        "only the hut's lintel and roof are open underneath");
    const auto statistics = environment.statistics();
    check(statistics.props == 18 && statistics.boxes == 36 && statistics.bytes == layout::environment_bytes &&
        statistics.occupied_cells > 0 && statistics.longest_list > 0 && statistics.longest_list <= 36, "statistics");

    bool listed = true;
    bool ceilings = true;
    for (float z = layout::environment_grid_min; z < -layout::environment_grid_min; z += 0.25f)
    {
        for (float x = layout::environment_grid_min; x < -layout::environment_grid_min; x += 0.25f)
        {
            const auto cell = environment.cell(x, z);
            for (std::uint32_t index = 0; index < boxes.size(); ++index)
            {
                const bool bit = (((index < 32 ? cell.mask_low : cell.mask_high) >> (index % 32)) & 1u) != 0;
                if (footprint_distance(boxes[index], x, z) <= layout::environment_reach) listed &= bit;
                if (bit) ceilings &= cell.ceiling >= top(boxes[index]) + layout::environment_reach;
            }
        }
    }
    check(listed, "every box within the reach of a point is listed in its cell");
    check(ceilings, "a cell's ceiling is at least every listed box's top plus the reach");
    bool tight = true;
    bool existing = true;
    const float diagonal = layout::environment_cell_size * std::sqrt(2.0f);
    for (std::uint32_t row = 0; row < layout::environment_grid_cells; ++row)
    {
        for (std::uint32_t column = 0; column < layout::environment_grid_cells; ++column)
        {
            const float x = layout::environment_grid_min + (static_cast<float>(column) + 0.5f) * layout::environment_cell_size;
            const float z = layout::environment_grid_min + (static_cast<float>(row) + 0.5f) * layout::environment_cell_size;
            const auto cell = environment.cell(x, z);
            const std::uint64_t mask = cell.mask_low | static_cast<std::uint64_t>(cell.mask_high) << 32;
            existing &= boxes.size() == 64 || (mask >> boxes.size()) == 0;
            for (std::uint32_t index = 0; index < boxes.size(); ++index)
            {
                if ((mask >> index) & 1u) tight &= footprint_distance(boxes[index], x, z) <= layout::environment_reach + diagonal;
            }
            if (mask == 0) existing &= cell.ceiling == Environment::empty_ceiling;
        }
    }
    check(tight, "no cell lists a box beyond the reach plus a cell diagonal");
    check(existing, "every listed bit names a box; empty cells have the empty ceiling");
    const auto outside = environment.cell(-64.5f, 0);
    const auto far = environment.cell(0, 64.0f);
    const auto invalid = environment.cell(std::numeric_limits<float>::quiet_NaN(), 0);
    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    check(outside.mask_low == 0 && far.mask_low == 0 && invalid.mask_low == 0 && outside.ceiling == Environment::empty_ceiling &&
        !grid_index(64.0f, 0) && !grid_index(-64.01f, 0) && !grid_index(0, 64.0f) && !grid_index(0, -64.01f) && !grid_index(nan, 0) &&
        !grid_index(0, nan) && grid_index(-64.0f, 63.99f).has_value(), "points outside the grid, and NaN, read an empty cell");
    const auto tank_edge = environment.cell(63.9f, 26);
    const auto past_tank = environment.cell(64.1f, 26);
    check((tank_edge.mask_low | tank_edge.mask_high) != 0 && past_tank.mask_low == 0 && past_tank.mask_high == 0,
        "the cell beside an occupied edge cell, just outside the grid, is empty");

    bool flat = true;
    bool bounded = true;
    bool grounded = true;
    bool supported = true;
    bool clear_view = true;
    for (const auto& box : boxes)
    {
        for (const auto& corner : corners(box))
        {
            bounded &= std::abs(corner.x) <= 61 && std::abs(corner.y) <= 61 && std::sqrt(corner.x * corner.x + corner.y * corner.y) <= 67;
            flat &= environment.terrain().height(corner.x, corner.y) == 0;
            if (top(box) > 1) clear_view &= corner.x < -15 || corner.x > 21 || corner.y < 41 || corner.y > 62;
        }
        if (bottom(box) < 0)
        {
            grounded &= bottom(box) <= environment.terrain().height(box.centre.x, box.centre.z) - 0.19f;
            continue;
        }
        bool resting = false;
        for (const auto& other : boxes)
        {
            if (&other == &box || other.prop != box.prop || !footprints_overlap(box, other, 0.01f)) continue;
            resting |= approx(top(other), bottom(box)) || approx(bottom(other), top(box));
        }
        supported &= resting;
    }
    check(bounded, "every footprint lies within +-61 m and 67 m of the centre");
    check(flat, "every footprint stands on the flat range");
    check(grounded, "grounded boxes reach below the ground");
    check(supported, "raised boxes rest on their supports within 1 mm");
    check(clear_view, "nothing taller than 1 m in the ground view's wedge");
    bool apart = true;
    for (std::size_t a = 0; a < boxes.size(); ++a)
    {
        for (std::size_t b = a + 1; b < boxes.size(); ++b)
        {
            if (boxes[a].prop != boxes[b].prop) apart &= !boxes_overlap(boxes[a], boxes[b], 1e-3f);
        }
    }
    check(apart, "no two props overlap");
    const auto crates = prop_index(environment, "crate stack");
    check(crates < props.size() && approx(top(boxes[props[crates].first_box + 2]), 2.4f), "the crate stack is 2.4 m high");

    static_assert(layout::prop_contact_limit + layout::prop_step_limit / 2 <= layout::environment_reach);
    static_assert(layout::prop_contact_limit + layout::prop_step_limit <= layout::environment_reach);
    static_assert(layout::prop_contact_limit + layout::deflection_band <= layout::environment_reach);
    float curl = 0;
    for (int i = 0; i < 40; ++i)
    {
        for (int j = 0; j < 40; ++j)
        {
            for (int k = 0; k < 40; ++k)
            {
                curl = std::max(curl, curl_magnitude({static_cast<float>(i) * 0.37f - 7, static_cast<float>(j) * 0.41f - 5,
                    static_cast<float>(k) * 0.43f + 3}));
            }
        }
    }
    constexpr float curl_allowance = 8;
    check(curl > 0.5f && curl <= curl_allowance, "the curl noise stays within its allowance");
    constexpr float strongest_wind = maximum_wind_speed * 2;
    constexpr float dt = 1.0f / 60.0f;
    float fastest = 0;
    float widest_spawn = 0;
    bool debris_small = true;
    for (std::uint32_t preset = 0; preset < preset_count; ++preset)
    {
        for (std::uint32_t surface = 0; surface < surface_type_count; ++surface)
        {
            const auto effect = make_effect_class(static_cast<ExplosionPreset>(preset), static_cast<SurfaceType>(surface));
            for (std::uint32_t index = 0; index < layer_count; ++index)
            {
                const auto& layer = effect.layers[index];
                if (layer.count == 0) continue;
                const float launch = layer.speed_min + layer.speed_range + layer.up_kick;
                float speed = launch + layer.gravity * (layer.lifetime_min + layer.lifetime_range + layer.delay_range);
                if (layer.drag > 0)
                {
                    const float vortex = effect.flow.strength / (2 * effect.flow.core_radius);
                    const float air = strongest_wind + vortex + layer.turbulence * curl_allowance;
                    speed = std::max(launch, air + (layer.buoyancy * layer.heat + layer.gravity) / layer.drag);
                }
                fastest = std::max(fastest, speed);
                widest_spawn = std::max(widest_spawn, layer.radius);
                if (index == static_cast<std::uint32_t>(EffectLayer::debris))
                {
                    debris_small &= 0.5f * (layer.size_min + layer.size_range) <= layout::prop_contact_limit;
                }
            }
        }
    }
    std::printf("fastest particle %.1f m/s, widest spawn %.2f m, curl noise sampled up to %.2f\n", fastest, widest_spawn, curl);
    check(fastest * dt <= layout::prop_step_limit, "no particle moves more than prop_step_limit in a step");
    check(widest_spawn / 2 <= layout::environment_reach, "a spawn segment is found from its ends");
    check(debris_small, "debris collides at half its size, within prop_contact_limit");

    const float clearance = std::max(6.0f, widest_spawn + layout::prop_contact_limit + 1);
    float nearest = std::numeric_limits<float>::max();
    const auto measure = [&](const ExplosionEvent& event) {
        for (const auto& box : boxes) nearest = std::min(nearest, footprint_distance(box, event.position.x, event.position.z));
    };
    for (const std::uint32_t count : {1u, 10u, 50u, 100u})
    {
        for (const auto& event : group_events(environment.terrain(), count)) measure(event);
    }
    for (const auto name : {"single", "grid-10", "grid-50", "grid-100", "barrage", "presets", "bombardment", "accumulation", "linger-barrage",
             "linger-saturation"})
    {
        for (const auto& scripted : make_scenario(name, environment)->events) measure(scripted.event);
    }
    std::printf("nearest scripted explosion %.2f m from a prop\n", nearest);
    check(nearest >= clearance, "the startup groups and every scenario but range keep 6 m from every prop");

    const auto scenario_fingerprint = [&environment](const char* name) {
        const auto scenario = make_scenario(name, environment, ExplosionPreset::artillery_he);
        std::vector<ExplosionEvent> events;
        std::vector<std::uint32_t> ticks;
        for (const auto& scripted : scenario->events)
        {
            events.push_back(scripted.event);
            ticks.push_back(scripted.tick);
        }
        return fingerprint(events, ticks) ^ scenario->duration_ticks;
    };
    check(scenario_fingerprint("single") == 0x99582b33583b2b6full && scenario_fingerprint("grid-10") == 0x9246e5ce76678e78ull &&
        scenario_fingerprint("grid-50") == 0xa1a405044b206195ull && scenario_fingerprint("grid-100") == 0xdd30e413978d4105ull &&
        scenario_fingerprint("barrage") == 0xa86c4b90ec96e8f4ull && scenario_fingerprint("presets") == 0x42f709503bf0f6c7ull,
        "the scripted event lists are M16's");
    check(scenario_fingerprint("bombardment") == 0x748a20614bf24c7eull, "the bombardment's event list is pinned");
    check(scenario_fingerprint("accumulation") == 0xf69bd9dfcedbf4e7ull, "the accumulation's event list is pinned");
    check(fingerprint(group_events(environment.terrain(), 100), {}) == 0x9810dcdfc42fd365ull,
        "the startup group is M16's");

    const auto range = make_scenario("range", environment, ExplosionPreset::heavy);
    const auto range_again = make_scenario("range", environment, ExplosionPreset::small_he);
    struct Expected
    {
        ExplosionPreset preset;
        SurfaceType surface;
        float3 normal;
        float3 surface_point;
    };
    const Expected expected[] = {
        {ExplosionPreset::artillery_he, SurfaceType::dirt, {0, 1, 0}, {-14, 0, -48.5f}},
        {ExplosionPreset::small_impact, SurfaceType::concrete, {0, 0, 1}, {-11, 1.5f, -53.8f}},
        {ExplosionPreset::small_he, SurfaceType::dirt, {0, 1, 0}, {-2, 2.4f, -50}},
        {ExplosionPreset::heavy, SurfaceType::dirt, {0, 1, 0}, {8, 0, -45.5f}},
        {ExplosionPreset::small_he, SurfaceType::concrete, {0, 1, 0}, {9.5f, 2.85f, -55}},
        {ExplosionPreset::dust_blast, SurfaceType::dirt, {0, 1, 0}, {-31.4f, 0, -45.7f}},
        {ExplosionPreset::fuel_fire, SurfaceType::dirt, {0, 1, 0}, {26.35f, 0, -47.93f}},
    };
    bool aimed = range && range->events.size() == std::size(expected) && range_again &&
        range_again->events.size() == range->events.size();
    std::uint32_t longest = 0;
    for (std::uint32_t index = 0; aimed && index < range->events.size(); ++index)
    {
        const auto& scripted = range->events[index];
        const auto& event = scripted.event;
        const auto& want = expected[index];
        const auto target = want.surface_point + want.normal * 0.5f;
        aimed = scripted.tick == 0 && event.seed == 3000 + index && event.preset == want.preset && event.surface == want.surface &&
            approx(event.normal.x, want.normal.x) && approx(event.normal.y, want.normal.y) && approx(event.normal.z, want.normal.z) &&
            approx(event.position.x, target.x) && approx(event.position.y, target.y) && approx(event.position.z, target.z) &&
            !environment.inside(event.position) && std::memcmp(&event, &range_again->events[index].event, sizeof(event)) == 0;
        longest = std::max(longest, effect_ticks(event.preset, event.surface));
    }
    check(aimed, "range: seven presets aimed at their surfaces, whatever the preset option");
    check(range && range->duration_ticks == longest && is_scenario_name("range"), "range runs until its longest effect ends");

    const auto down = [](float x, float z) { return Ray{{x, 20, z}, {0, -1, 0}}; };
    constexpr float infinity = std::numeric_limits<float>::infinity();
    const auto crate_top = environment.raycast(down(-2, -50), infinity, true);
    check(crate_top && approx(crate_top->point.y, 2.4f) && approx(crate_top->normal.y, 1) && crate_top->surface == SurfaceType::dirt &&
        crate_top->prop == crates, "down onto the crate stack: its top, facing up, wood's dirt response");
    const auto wall = environment.raycast({{-11, 1.5f, -40}, {0, 0, -1}}, infinity, true);
    check(wall && approx(wall->point.z, -53.8f) && approx(wall->normal.z, 1) && wall->surface == SurfaceType::concrete &&
        wall->prop == prop_index(environment, "blast wall A"), "along -z onto wall A's south face");
    const auto door = environment.raycast({{8, 1, -40}, {0, 0, -1}}, infinity, true);
    check(door && approx(door->point.z, -56.2f) && approx(door->normal.z, 1) && door->prop == hut,
        "through the hut's door onto the inside of its north wall");
    const auto roof = environment.raycast(down(9.5f, -55), infinity, true);
    check(roof && approx(roof->point.y, 2.85f) && approx(roof->normal.y, 1) && roof->surface == SurfaceType::concrete && roof->prop == hut,
        "down onto the hut's roof");
    const auto truck = prop_index(environment, "truck");
    const auto& cargo = boxes[props[truck].first_box];
    const auto side = from_box(cargo, {0, 0, 1});
    const auto truck_hit = environment.raycast({cargo.centre + side * 10, -side}, infinity, true);
    check(truck_hit && truck_hit->prop == truck && approx(truck_hit->normal.x, side.x) && approx(truck_hit->normal.z, side.z) &&
        approx(truck_hit->normal.y, 0) && approx(footprint_distance(cargo, truck_hit->point.x, truck_hit->point.z), 0),
        "onto the yawed truck's side: the normal is its turned axis");
    const auto from_inside = environment.raycast({boxes[props[crates].first_box].centre, {0, -1, 0}}, infinity, true);
    check(from_inside && !from_inside->prop && approx(from_inside->point.y, 0), "a ray starting inside a crate ignores it");
    const auto slanted = environment.raycast({{-14, 3, -24}, normalize(float3{0, -1, -2})}, infinity, true);
    check(slanted && !slanted->prop && approx(slanted->point.y, 0) && slanted->point.z > -40, "the ground before a far wall");
    const auto limited = environment.raycast({{-11, 1.5f, -40}, {0, 0, -1}}, 10, true);
    check(!limited, "nothing within max_distance");
    bool terrain_only = true;
    bool on_faces = true;
    std::uint32_t prop_hits = 0;
    for (int i = 0; i < 400; ++i)
    {
        const float angle = static_cast<float>(i) * 0.731f;
        const float3 origin{-20 + static_cast<float>(i % 20) * 3, 1 + static_cast<float>(i % 7), -30 - static_cast<float>(i % 11)};
        const Ray ray{origin, normalize(float3{std::cos(angle), -0.05f - 0.02f * static_cast<float>(i % 5), std::sin(angle) - 0.6f})};
        const auto bare = environment.raycast(ray, infinity, false);
        const auto ground = environment.terrain().raycast(ray, infinity);
        terrain_only &= bare.has_value() == ground.has_value() && (!bare || (bare->point.x == ground->x && bare->point.y == ground->y &&
            bare->point.z == ground->z && !bare->prop));
        const auto hit = environment.raycast(ray, infinity, true);
        if (!hit || !hit->prop) continue;
        ++prop_hits;
        bool on_face = false;
        for (std::uint32_t index = props[*hit->prop].first_box; index < props[*hit->prop].first_box + props[*hit->prop].box_count; ++index)
        {
            const auto& box = boxes[index];
            const auto local = to_box(box, hit->point - box.centre);
            const bool within = std::abs(local.x) <= box.half_extent.x + 1e-3f && std::abs(local.y) <= box.half_extent.y + 1e-3f &&
                std::abs(local.z) <= box.half_extent.z + 1e-3f;
            const bool face = approx(std::abs(local.x), box.half_extent.x) || approx(std::abs(local.y), box.half_extent.y) ||
                approx(std::abs(local.z), box.half_extent.z);
            on_face |= within && face;
        }
        on_faces &= on_face && approx(length(hit->normal), 1) && dot(hit->normal, ray.direction) < 0;
    }
    check(terrain_only, "without props a ray meets the terrain as Terrain::raycast does");
    check(prop_hits > 20 && on_faces, "every prop hit lies on a face of its box, facing the ray");
    check(!environment.inside(down(-2, -50).origin) && environment.inside(boxes[props[crates].first_box].centre) &&
        !environment.inside(float3{-2, 2.4f, -50}), "inside is strict");

    const auto& barrier = boxes[props[prop_index(environment, "jersey barrier")].first_box];
    const auto end = barrier.centre + from_box(barrier, {barrier.half_extent.x, 0.5f - barrier.centre.y, 0});
    const auto end_normal = from_box(barrier, {1, 0, 0});
    const auto origin = environment.event_origin({end, end_normal, SurfaceType::concrete, 0});
    check(!environment.inside(origin) && dot(origin - end, end_normal) > 0.05f && dot(origin - end, end_normal) <= 0.1f,
        "an origin that would be inside the next barrier is bisected out of it");
    const auto open_ground = environment.event_origin({{0, 0, 0}, {0, 1, 0}, SurfaceType::dirt, std::nullopt});
    check(open_ground.x == 0 && open_ground.y == 0.5f && open_ground.z == 0, "elsewhere the origin is 0.5 m off the surface");

    const auto mesh = build_prop_mesh(environment);
    check(mesh.size() % 3 == 0 && mesh.size() == 18 * 36 + 7 * 84 + 4 * 132, "a triangle list: 18 boxes, 7 barriers, 4 rocks");
    std::size_t first = 0;
    bool closed = true;
    bool outward = true;
    bool enclosed = true;
    bool materials = true;
    for (const auto& prop : props)
    {
        const std::size_t count = prop.shape == PropShape::box ? 36u * prop.box_count : prop.shape == PropShape::jersey_barrier ? 84u : 132u;
        const std::size_t part = prop.shape == PropShape::box ? 36u : count;
        for (std::size_t start = first; start < first + count; start += part)
        {
            std::vector<std::pair<std::size_t, std::size_t>> edges;
            const auto same = [&mesh](std::size_t a, std::size_t b) {
                return std::memcmp(&mesh[a].position, &mesh[b].position, sizeof(float3)) == 0;
            };
            for (std::size_t vertex = start; vertex < start + part; vertex += 3)
            {
                for (std::size_t corner = 0; corner < 3; ++corner) edges.push_back({vertex + corner, vertex + (corner + 1) % 3});
            }
            for (const auto& [from, to] : edges)
            {
                std::size_t forward = 0;
                std::size_t reverse = 0;
                for (const auto& [a, b] : edges)
                {
                    forward += same(a, from) && same(b, to) ? 1 : 0;
                    reverse += same(a, to) && same(b, from) ? 1 : 0;
                }
                closed &= forward == 1 && reverse == 1;
            }
        }
        for (std::size_t vertex = first; vertex < first + count; vertex += 3)
        {
            const auto& box = boxes[prop.first_box + (prop.shape == PropShape::box ? (vertex - first) / 36 : 0)];
            const auto inner = prop.shape == PropShape::jersey_barrier ? float3{box.centre.x, 0.3f, box.centre.z} : box.centre;
            const auto a = mesh[vertex].position;
            const auto b = mesh[vertex + 1].position;
            const auto c = mesh[vertex + 2].position;
            const auto winding = normalize(cross(b - a, c - a));
            const auto centroid = (a + b + c) / 3.0f;
            for (std::size_t corner = 0; corner < 3; ++corner)
            {
                const auto& normal = mesh[vertex + corner].normal;
                outward &= approx(length(normal), 1, 1e-4f) && dot(normal, winding) > 0.999f && dot(normal, centroid - inner) > 0;
            }
        }
        for (std::size_t vertex = first; vertex < first + count; ++vertex)
        {
            bool within = false;
            for (std::uint32_t index = prop.first_box; index < prop.first_box + prop.box_count; ++index)
            {
                const auto& box = boxes[index];
                const auto local = to_box(box, mesh[vertex].position - box.centre);
                within |= std::abs(local.x) <= box.half_extent.x + 1e-3f && std::abs(local.y) <= box.half_extent.y + 1e-3f &&
                    std::abs(local.z) <= box.half_extent.z + 1e-3f;
            }
            enclosed &= within;
            materials &= mesh[vertex].material == static_cast<std::uint32_t>(prop.material) && mesh[vertex].shade > 0.3f;
        }
        first += count;
    }
    check(first == mesh.size(), "the mesh holds every prop in order");
    check(closed, "every part is closed and consistently wound");
    check(outward, "unit normals, facing out and matching the winding");
    check(enclosed, "every vertex lies within its prop's collision boxes");
    check(materials, "each vertex carries its prop's material and a positive shade");

    if (failures == 0) std::puts("environment tests passed");
    return failures == 0 ? 0 : 1;
}
