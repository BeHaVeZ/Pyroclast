#include "jet.hpp"
#include "simulation_clock.hpp"
#include "explosion_presets.hpp"
#include "../graphics/particle_layout.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace pyroclast
{
namespace
{

double smoothstep(double edge0, double edge1, double x)
{
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

double3 horizontal(double3 a)
{
    const double size = std::sqrt(a.x * a.x + a.z * a.z);
    return size > 1e-6 ? double3{a.x / size, 0, a.z / size} : double3{1, 0, 0};
}

double3 to_double3(float4 a)
{
    return {a.x, a.y, a.z};
}

// Rosenhead-regularised Biot-Savart segment; kept in step with jet_segment in jet_flow.hlsli.
double3 segment_air(double3 point, double3 a, double3 b, double gamma, double core)
{
    const double3 r0 = b - a;
    const double3 r1 = point - a;
    const double3 r2 = point - b;
    const double3 c = cross(r1, r2);
    const double l1 = length(r1);
    const double l2 = length(r2);
    const double denominator = dot(c, c) + core * core * dot(r0, r0);
    if (denominator < 1e-9 || l1 < 1e-6 || l2 < 1e-6) return {};
    return c * (gamma / (4 * jet::pi) * dot(r0, r1 * (1 / l1) - r2 * (1 / l2)) / denominator);
}

bool near_box(double3 point, double3 a, double3 b, double3 c, double3 d, double window)
{
    const double low_x = std::min({a.x, b.x, c.x, d.x}) - window;
    const double high_x = std::max({a.x, b.x, c.x, d.x}) + window;
    const double low_y = std::min({a.y, b.y, c.y, d.y}) - window;
    const double high_y = std::max({a.y, b.y, c.y, d.y}) + window;
    const double low_z = std::min({a.z, b.z, c.z, d.z}) - window;
    const double high_z = std::max({a.z, b.z, c.z, d.z}) + window;
    return point.x >= low_x && point.x <= high_x && point.y >= low_y && point.y <= high_y && point.z >= low_z && point.z <= high_z;
}

double3 mirrored(double3 a, double ground)
{
    return {a.x, 2 * ground - a.y, a.z};
}

double3 legs_air(double3 point, double3 newer_left, double3 newer_right, double newer_gamma, double newer_core, double3 older_left,
    double3 older_right, double older_gamma, double older_core, double ground, double image, double3 newer_centre, double newer_speed,
    double newer_radius, double3 older_centre, double older_speed, double older_radius)
{
    double3 air;
    const double gamma = 0.5 * (newer_gamma + older_gamma);
    const double core = 0.5 * (newer_core + older_core);
    if (near_box(point, newer_left, newer_right, older_left, older_right, jet_layout::jet_window))
    {
        air = air + segment_air(point, older_left, newer_left, gamma, core) + segment_air(point, newer_right, older_right, gamma, core);
        if (image > 0)
        {
            air = air + segment_air(point, mirrored(older_left, ground), mirrored(newer_left, ground), -gamma * image, core) +
                segment_air(point, mirrored(newer_right, ground), mirrored(older_right, ground), -gamma * image, core);
        }
    }
    const double3 along = newer_centre - older_centre;
    const double reach = length(along);
    if (reach > 1e-6)
    {
        const double3 direction = along * (1 / reach);
        const double t = std::clamp(dot(point - older_centre, direction) / reach, 0.0, 1.0);
        const double3 nearest = older_centre + along * t;
        const double3 offset = point - nearest;
        const double radius = older_radius + (newer_radius - older_radius) * t;
        const double speed = older_speed + (newer_speed - older_speed) * t;
        const double squared = dot(offset, offset);
        if (radius > 0 && squared < 9 * radius * radius) air = air - direction * (speed * std::exp(-squared / (radius * radius)));
    }
    return air;
}
}

const JetPass* listed_pass(const JetFlight& flight, std::uint32_t tick)
{
    for (const auto& pass : flight.passes)
    {
        if (tick >= pass.list_tick && tick < pass.unlist_tick) return &pass;
    }
    return nullptr;
}

JetPose jet_pose(const JetFlight& flight, std::uint32_t tick, const Terrain& terrain, const WindSettings& wind, bool gusts, std::uint32_t mask)
{
    JetPose pose;
    const JetPass* pass = listed_pass(flight, tick);
    if (!pass) return pose;
    const auto state = flight_state(*pass, tick);
    pose.listed = true;
    pose.fade = helicopter_fade(*pass, tick);
    pose.centre = state.position;
    pose.velocity = state.velocity;
    const double ground = terrain.height(static_cast<float>(state.position.x), static_cast<float>(state.position.z));
    const double3 air = state.velocity - lingering_wind_at(wind, tick, gusts, state.position.y - ground, mask);
    pose.airspeed = length(air);
    const double3 forward = pose.airspeed > 1e-3 ? air * (1 / pose.airspeed) : horizontal(state.velocity);
    const double3 needed = state.acceleration + double3{0, jet::gravity, 0};
    double3 lift = needed - forward * dot(needed, forward);
    if (length(lift) < 1e-6) lift = double3{0, 1, 0} - forward * forward.y;
    const double sense = dot(lift, double3{0, 1, 0} - forward * forward.y) < 0 ? -1.0 : 1.0;
    pose.load = sense * length(lift) / jet::gravity;
    const double3 up = normalize(lift) * sense;
    const double speed_ratio = jet::pass_speed / std::max(pose.airspeed, 60.0);
    const double attack = std::clamp(jet::attack_per_g * pose.load * speed_ratio * speed_ratio, -12 * jet::pi / 180, 12 * jet::pi / 180);
    pose.forward = forward * std::cos(attack) + up * std::sin(attack);
    pose.up = up * std::cos(attack) - forward * std::sin(attack);
    pose.right = cross(pose.forward, pose.up);
    const double dynamic = 0.5 * jet::density * pose.airspeed * pose.airspeed;
    const double weight = pose.load * jet::mass * jet::gravity;
    const double induced = dynamic > 1 ? weight * weight / (dynamic * jet::pi * jet::efficiency * jet::span * jet::span) : 0;
    pose.thrust = std::max(0.0, dynamic * jet::drag_area + induced + jet::mass * dot(state.acceleration, forward));
    pose.mach = pose.airspeed / jet_layout::jet_sound_speed;
    pose.afterburner = smoothstep(jet_layout::jet_dry_thrust, jet_layout::jet_wet_thrust, pose.thrust);
    return pose;
}

double vapor_strength(const JetPose& pose, double humidity)
{
    if (!pose.listed) return 0;
    return smoothstep(jet_layout::jet_vapor_mach, jet_layout::jet_vapor_mach + 0.08, pose.mach) *
        std::clamp((humidity - jet_layout::jet_vapor_humidity) / (1 - jet_layout::jet_vapor_humidity), 0.0, 1.0) * pose.fade;
}

// The circulation takes |load|: the GPU's wake lists positive circulations only.
std::optional<KnotBirth> knot_birth(const JetFlight& flight, std::uint32_t tick, const Terrain& terrain, const WindSettings& wind, bool gusts,
    std::uint32_t mask)
{
    const auto pose = jet_pose(flight, tick, terrain, wind, gusts, mask);
    if (!pose.listed || pose.airspeed < 1) return std::nullopt;
    KnotBirth birth;
    birth.tick = tick;
    birth.pass = static_cast<std::uint32_t>(listed_pass(flight, tick) - flight.passes.data());
    birth.centre = pose.centre;
    birth.forward = horizontal(pose.velocity);
    birth.right = pose.right;
    birth.ground = terrain.height(static_cast<float>(pose.centre.x), static_cast<float>(pose.centre.z));
    birth.gamma = std::abs(pose.load) * jet::mass * jet::gravity / (jet::density * pose.airspeed * jet::spacing) * pose.fade;
    birth.exhaust = pose.thrust / pose.airspeed * pose.fade;
    return birth;
}

PairRate pair_rate(double height, double half_spacing, double gamma)
{
    const double h = std::max(height, 0.05);
    const double s = std::max(half_spacing, 0.05);
    const double squared = s * s + h * h;
    const double scale = gamma / (4 * jet::pi);
    return {-scale * h * h / (s * squared), scale * s * s / (h * squared)};
}

double knot_gamma(const KnotBirth& birth, double age)
{
    return age <= jet::decay_start ? birth.gamma : birth.gamma * std::exp(-(age - jet::decay_start) / jet::decay_time);
}

double knot_core(double age)
{
    return std::sqrt(jet::core_radius * jet::core_radius + 4 * jet::core_viscosity * age);
}

double exhaust_radius(double age)
{
    return std::sqrt(jet::nozzle_radius * jet::nozzle_radius + jet::mixing * age);
}

double exhaust_speed(double exhaust, double age)
{
    const double radius = exhaust_radius(age);
    return exhaust / (jet::density * jet::pi * radius * radius) * std::exp(-age / jet::exhaust_time);
}

namespace
{
void advance_pair(const KnotBirth& birth, double age, double seconds, double& height, double& half_spacing)
{
    const auto rate = [&birth](double at, double h, double s) { return pair_rate(h, s, knot_gamma(birth, at)); };
    const auto k1 = rate(age, height, half_spacing);
    const auto k2 = rate(age + 0.5 * seconds, height + 0.5 * seconds * k1.height, half_spacing + 0.5 * seconds * k1.half_spacing);
    const auto k3 = rate(age + 0.5 * seconds, height + 0.5 * seconds * k2.height, half_spacing + 0.5 * seconds * k2.half_spacing);
    const auto k4 = rate(age + seconds, height + seconds * k3.height, half_spacing + seconds * k3.half_spacing);
    height += seconds / 6 * (k1.height + 2 * k2.height + 2 * k3.height + k4.height);
    half_spacing += seconds / 6 * (k1.half_spacing + 2 * k2.half_spacing + 2 * k3.half_spacing + k4.half_spacing);
}
}

KnotState JetHistory::state(const KnotBirth& birth, std::uint32_t age_ticks, const WindSettings& wind, bool gusts, std::uint32_t mask)
{
    auto& entry = entries_[(birth.tick / jet_layout::jet_knot_ticks) % length];
    const std::uint32_t full = age_ticks / jet::integration_ticks;
    if (entry.tick != birth.tick || entry.steps > full)
    {
        entry = {};
        entry.tick = birth.tick;
        entry.height = birth.centre.y - birth.ground;
        entry.half_spacing = 0.5 * jet::spacing;
    }
    const double step = jet::integration_ticks * tick_seconds;
    while (entry.steps < full)
    {
        const double age = entry.steps * step;
        const double3 blown = lingering_wind_at(wind, birth.tick + entry.steps * jet::integration_ticks, gusts, entry.height, mask);
        entry.drift = entry.drift + double3{blown.x, 0, blown.z} * step;
        advance_pair(birth, age, step, entry.height, entry.half_spacing);
        ++entry.steps;
    }
    KnotState state;
    state.height = entry.height;
    state.half_spacing = entry.half_spacing;
    state.drift = entry.drift;
    const std::uint32_t rest = age_ticks % jet::integration_ticks;
    if (rest != 0)
    {
        const double seconds = rest * tick_seconds;
        const double3 blown = lingering_wind_at(wind, birth.tick + entry.steps * jet::integration_ticks, gusts, entry.height, mask);
        state.drift = state.drift + double3{blown.x, 0, blown.z} * seconds;
        advance_pair(birth, entry.steps * step, seconds, state.height, state.half_spacing);
    }
    const double age = age_ticks * tick_seconds;
    state.gamma = knot_gamma(birth, age);
    state.core = knot_core(age);
    state.exhaust_speed = exhaust_speed(birth.exhaust, age);
    state.exhaust_radius = exhaust_radius(age);
    state.centre = {birth.centre.x + state.drift.x, birth.ground + state.height, birth.centre.z + state.drift.z};
    state.left = state.centre - birth.right * state.half_spacing;
    state.right = state.centre + birth.right * state.half_spacing;
    return state;
}

void JetHistory::forget_after(std::uint32_t tick)
{
    for (auto& entry : entries_)
    {
        if (entry.tick != never && entry.tick > tick) entry = {};
    }
}

void JetHistory::clear()
{
    entries_.fill({});
}

namespace
{
void grow(JetWake& wake, double3 point)
{
    const float3 p = to_float3(point);
    if (wake.empty)
    {
        wake.box_min = p;
        wake.box_max = p;
        wake.empty = false;
        return;
    }
    wake.box_min = {std::min(wake.box_min.x, p.x), std::min(wake.box_min.y, p.y), std::min(wake.box_min.z, p.z)};
    wake.box_max = {std::max(wake.box_max.x, p.x), std::max(wake.box_max.y, p.y), std::max(wake.box_max.z, p.z)};
}

bool in_range(double3 point)
{
    const double edge = jet::range_box + 60;
    return std::abs(point.x) <= edge && std::abs(point.z) <= edge;
}
}

JetWake jet_wake(const JetFlight& flight, std::uint32_t tick, const Terrain& terrain, const WindSettings& wind, bool gusts, std::uint32_t mask,
    JetHistory& history)
{
    JetWake wake;
    const std::uint32_t newest = tick - tick % jet_layout::jet_knot_ticks;
    const auto oldest_age = static_cast<std::uint32_t>(jet::knot_life * ticks_per_second);
    std::array<std::uint32_t, jet_layout::jet_knot_limit> ticks{};
    std::array<std::uint32_t, jet_layout::jet_knot_limit> passes{};
    for (std::uint32_t age = tick - newest; age <= oldest_age && age <= tick && wake.count < jet_layout::jet_knot_limit; age += jet_layout::jet_knot_ticks)
    {
        const std::uint32_t born = tick - age;
        const auto birth = knot_birth(flight, born, terrain, wind, gusts, mask);
        if (!birth || !in_range(birth->centre) || birth->gamma <= 0) continue;
        const auto state = history.state(*birth, age, wind, gusts, mask);
        if (!in_range(state.centre)) continue;
        auto& record = wake.knots[wake.count];
        const double image = 1 - smoothstep(jet::image_near, jet::image_far, state.height);
        const double taper = std::min(1.0, static_cast<double>(jet_layout::jet_knot_limit - wake.count) / jet::taper_knots);
        record.words[0] = {static_cast<float>(state.left.x), static_cast<float>(state.left.y), static_cast<float>(state.left.z),
            static_cast<float>(state.gamma * taper)};
        record.words[1] = {static_cast<float>(state.right.x), static_cast<float>(state.right.y), static_cast<float>(state.right.z), static_cast<float>(state.core)};
        record.words[2] = {static_cast<float>(birth->ground), static_cast<float>(image), static_cast<float>(state.exhaust_speed),
            static_cast<float>(state.exhaust_radius)};
        record.words[3] = {0, static_cast<float>(age * tick_seconds), 0, 0};
        ticks[wake.count] = born;
        passes[wake.count] = birth->pass;
        grow(wake, state.left);
        grow(wake, state.right);
        if (image > 0)
        {
            grow(wake, mirrored(state.left, birth->ground));
            grow(wake, mirrored(state.right, birth->ground));
        }
        ++wake.count;
    }
    for (std::uint32_t knot = 0; knot + 1 < wake.count; ++knot)
    {
        if (ticks[knot] - ticks[knot + 1] == jet_layout::jet_knot_ticks && passes[knot] == passes[knot + 1]) wake.knots[knot].words[3].x = 1;
    }
    wake.joined = wake.count != 0 && ticks[0] == newest;
    return wake;
}

JetHead jet_head(const JetPose& pose)
{
    JetHead head;
    if (!pose.listed || pose.fade <= 0 || pose.airspeed < 1) return head;
    const double3 span_axis = pose.right * (0.5 * jet::spacing);
    const double3 left = pose.centre - span_axis;
    const double3 right = pose.centre + span_axis;
    const double gamma = std::abs(pose.load) * jet::mass * jet::gravity / (jet::density * pose.airspeed * jet::spacing) * pose.fade;
    head.words[0] = {static_cast<float>(left.x), static_cast<float>(left.y), static_cast<float>(left.z), static_cast<float>(gamma)};
    head.words[1] = {static_cast<float>(right.x), static_cast<float>(right.y), static_cast<float>(right.z), static_cast<float>(jet::core_radius)};
    head.words[2] = {static_cast<float>(pose.centre.x), static_cast<float>(pose.centre.y), static_cast<float>(pose.centre.z), 0};
    head.words[3] = {static_cast<float>(pose.velocity.x), static_cast<float>(pose.velocity.y), static_cast<float>(pose.velocity.z),
        static_cast<float>(pose.thrust / pose.airspeed * pose.fade)};
    return head;
}

void include_head(JetWake& wake, const JetHead& head)
{
    if (head.words[0].w <= 0) return;
    const double3 forward = normalize(to_double3(head.words[3]));
    const double3 centre = to_double3(head.words[2]);
    grow(wake, to_double3(head.words[0]));
    grow(wake, to_double3(head.words[1]));
    grow(wake, centre);
    grow(wake, centre - forward * jet::nozzle_aft);
}

double3 jet_air(const JetWake& wake, const JetHead& head, double3 point)
{
    if (wake.empty) return {};
    const double window = jet_layout::jet_window;
    if (point.x < wake.box_min.x - window || point.x > wake.box_max.x + window || point.y < wake.box_min.y - window ||
        point.y > wake.box_max.y + window || point.z < wake.box_min.z - window || point.z > wake.box_max.z + window)
    {
        return {};
    }
    double3 air;
    const auto centre_of = [](const JetKnotRecord& knot) { return (to_double3(knot.words[0]) + to_double3(knot.words[1])) * 0.5; };
    for (std::uint32_t knot = 0; knot + 1 < wake.count; ++knot)
    {
        const auto& newer = wake.knots[knot];
        const auto& older = wake.knots[knot + 1];
        if (newer.words[3].x == 0) continue;
        air = air + legs_air(point, to_double3(newer.words[0]), to_double3(newer.words[1]), newer.words[0].w, newer.words[1].w,
                        to_double3(older.words[0]), to_double3(older.words[1]), older.words[0].w, older.words[1].w,
                        0.5 * (newer.words[2].x + older.words[2].x), 0.5 * (newer.words[2].y + older.words[2].y), centre_of(newer),
                        newer.words[2].z, newer.words[2].w, centre_of(older), older.words[2].z, older.words[2].w);
    }
    const double gamma = head.words[0].w;
    if (gamma > 0)
    {
        const double3 left = to_double3(head.words[0]);
        const double3 right = to_double3(head.words[1]);
        const double core = head.words[1].w;
        const double3 centre = to_double3(head.words[2]);
        const double3 velocity = to_double3(head.words[3]);
        const double speed = length(velocity);
        const double3 forward = velocity * (1 / std::max(speed, 1e-3));
        const double ground = wake.count != 0 ? wake.knots[0].words[2].x : centre.y - 1000;
        const double image = 1 - smoothstep(jet::image_near, jet::image_far, centre.y - ground);
        if (near_box(point, left, right, left, right, window))
        {
            air = air + segment_air(point, left, right, gamma, core);
            if (image > 0) air = air + segment_air(point, mirrored(left, ground), mirrored(right, ground), -gamma * image, core);
        }
        if (wake.joined)
        {
            const auto& older = wake.knots[0];
            const double exhaust = head.words[3].w;
            const double nozzle_speed = exhaust / (jet::density * jet::pi * jet::nozzle_radius * jet::nozzle_radius);
            air = air + legs_air(point, left, right, gamma, core, to_double3(older.words[0]), to_double3(older.words[1]), older.words[0].w,
                            older.words[1].w, ground, image, centre - forward * jet::nozzle_aft, nozzle_speed, jet::nozzle_radius,
                            centre_of(older), older.words[2].z, older.words[2].w);
        }
        const double3 offset = point - centre;
        double distance = length(offset);
        if (distance < jet_layout::jet_body_window && distance > 1e-6)
        {
            const double3 normal = offset * (1 / distance);
            distance = std::max(distance, static_cast<double>(jet_layout::jet_body_radius));
            const double a = jet_layout::jet_body_radius;
            air = air + (normal * (3 * dot(velocity, normal)) - velocity) * (a * a * a / (2 * distance * distance * distance));
        }
    }
    const double speed = length(air);
    if (speed > jet_layout::jet_speed_limit) air = air * (jet_layout::jet_speed_limit / speed);
    return air;
}

JetEmission jet_emission(const JetFlight& flight, std::uint32_t tick, const Terrain& terrain, const WindSettings& wind, bool gusts, std::uint32_t mask,
    JetHistory& history)
{
    JetEmission emission;
    const auto wake = jet_wake(flight, tick, terrain, wind, gusts, mask, history);
    const double threshold = jet_layout::jet_saltation;
    double total = 0;
    double spread = 0;
    double weight = 0;
    bool found = false;
    for (std::uint32_t knot = 0; knot + 1 < wake.count; ++knot)
    {
        const auto& newer = wake.knots[knot];
        const auto& older = wake.knots[knot + 1];
        if (newer.words[3].x == 0) continue;
        const double3 centre = (to_double3(newer.words[0]) + to_double3(newer.words[1])) * 0.5;
        const double3 previous = (to_double3(older.words[0]) + to_double3(older.words[1])) * 0.5;
        const double ground = newer.words[2].x;
        const double height = std::max(centre.y - ground, 0.5);
        const double vortex = newer.words[0].w / (jet::pi * height);
        const double radius = std::max(static_cast<double>(newer.words[2].w), jet::nozzle_radius);
        const double exhaust = newer.words[2].z * std::exp(-height * height / (radius * radius));
        const double u = std::max(vortex, exhaust);
        if (u <= threshold) continue;
        const double track = std::hypot(centre.x - previous.x, centre.z - previous.z);
        const double flux = u * (u * u - threshold * threshold) * track;
        total += flux;
        spread += 0.5 * length(to_double3(newer.words[1]) - to_double3(newer.words[0])) * flux;
        weight += flux;
        const float2 point{static_cast<float>(centre.x), static_cast<float>(centre.z)};
        if (!found) emission.dust_from = point;
        emission.dust_to = point;
        found = true;
    }
    if (found && total > 0)
    {
        emission.dust = static_cast<std::uint32_t>(std::lround(1024 * std::min(1.0, total / jet_layout::jet_reference_flux)));
        emission.half_width = static_cast<float>(spread / weight);
    }
    return emission;
}

DustCycle jet_cycle(std::uint32_t seed, std::uint32_t local)
{
    const std::uint32_t id = seed * particle_layout::explosion_particles + local;
    DustCycle cycle;
    cycle.length = jet_layout::jet_cycle_min_ticks + particle_hash(id ^ jet_layout::jet_salt_cycle) % jet_layout::jet_cycle_range_ticks;
    cycle.phase = particle_hash(id ^ jet_layout::jet_salt_phase) % cycle.length;
    return cycle;
}

bool jet_born(std::uint32_t seed, std::uint32_t local, std::uint32_t cycle, std::uint32_t emission)
{
    const std::uint32_t id = seed * particle_layout::explosion_particles + local;
    return (particle_hash(id ^ particle_hash(cycle ^ jet_layout::jet_salt_birth)) & 1023u) < emission;
}

const JetEmissionHistory::Entry* JetEmissionHistory::find(std::uint32_t tick) const
{
    const auto& entry = entries_[tick % length];
    return entry.tick == tick ? &entry : nullptr;
}

void JetEmissionHistory::store(std::uint32_t tick, const JetEmission& emission, float3 point)
{
    auto& entry = entries_[tick % length];
    entry.tick = tick;
    entry.dust = emission.dust;
    entry.point = point;
}

void JetEmissionHistory::clear()
{
    entries_.fill({});
}

JetCensus jet_census(const JetEmissionHistory& history, std::uint32_t seed, std::uint32_t start_tick, std::uint32_t end_tick, std::uint32_t tick,
    bool dust_drawn)
{
    JetCensus census;
    if (tick >= end_tick || !dust_drawn) return census;
    double3 point;
    for (std::uint32_t local = 0; local < particle_layout::explosion_particles; ++local)
    {
        const auto cycle = jet_cycle(seed, local);
        const std::uint32_t first = start_tick + 1 + cycle.phase;
        if (tick < first) continue;
        const std::uint32_t index = (tick - first) / cycle.length;
        const std::uint32_t begun = first + index * cycle.length;
        const auto* entry = history.find(begun);
        if (!entry || !jet_born(seed, local, index, entry->dust)) continue;
        ++census.dust;
        point = point + to_double3(entry->point);
    }
    if (census.dust != 0) census.mean_point = point * (1.0 / census.dust);
    return census;
}

void Jetcraft::fly(JetFlight flight, std::uint32_t seed)
{
    flight_ = std::move(flight);
    variants_ = true;
    request_pending_ = true;
    seed_ = seed;
    history_.clear();
    emissions_.clear();
}

void Jetcraft::add_pass(JetPass pass, std::uint32_t seed)
{
    if (!flight_)
    {
        fly({"sandbox", {std::move(pass)}}, seed);
        return;
    }
    flight_->passes.push_back(std::move(pass));
    request_pending_ = true;
}

bool Jetcraft::take_over(std::uint32_t tick)
{
    if (!flight_) return false;
    auto& passes = flight_->passes;
    const auto listed = std::find_if(passes.begin(), passes.end(), [tick](const JetPass& pass) { return tick >= pass.list_tick && tick < pass.unlist_tick; });
    if (listed == passes.end())
    {
        std::erase_if(passes, [tick](const JetPass& pass) { return pass.list_tick > tick; });
        return false;
    }
    passes.erase(listed + 1, passes.end());
    auto& pass = passes.back();
    continue_plan(pass, tick, {});
    pass.unlist_tick = never;
    history_.forget_after(tick);
    request_pending_ = true;
    return true;
}

std::uint32_t Jetcraft::end_passes(std::uint32_t tick, std::uint32_t fade_ticks)
{
    if (!flight_) return tick;
    auto& passes = flight_->passes;
    std::erase_if(passes, [tick](const JetPass& pass) { return pass.list_tick > tick; });
    std::uint32_t free = tick;
    for (auto& pass : passes)
    {
        if (tick < pass.list_tick || tick >= pass.unlist_tick) continue;
        if (pass.unlist_tick > tick + fade_ticks)
        {
            continue_plan(pass, tick, {});
            FlightBuilder builder(pass);
            builder.cruise(fade_ticks);
            pass = builder.build();
            pass.unlist_tick = tick + fade_ticks;
            history_.forget_after(tick);
        }
        free = std::max(free, pass.unlist_tick);
    }
    request_pending_ = true;
    return free;
}

void Jetcraft::steer(std::uint32_t tick, std::span<const FlightKnot> knots)
{
    assert(flight_ && !flight_->passes.empty());
    continue_plan(flight_->passes.back(), tick, knots);
    history_.forget_after(tick);
}

void Jetcraft::clear()
{
    flight_.reset();
    variants_ = false;
    request_pending_ = false;
    history_.clear();
    emissions_.clear();
}

std::uint32_t Jetcraft::first_list_tick() const
{
    std::uint32_t first = never;
    if (flight_)
    {
        for (const auto& pass : flight_->passes) first = std::min(first, pass.list_tick);
    }
    return first;
}

std::uint32_t Jetcraft::slot_end() const
{
    if (!flight_ || flight_->passes.empty()) return never;
    std::uint32_t last = 0;
    for (const auto& pass : flight_->passes)
    {
        if (pass.unlist_tick == never) return never;
        last = std::max(last, pass.unlist_tick);
    }
    return last + JetEmissionHistory::length;
}

JetPass straight_pass(std::string name, double3 over, double3 heading, double speed, std::uint32_t centre_tick, double lead)
{
    heading = horizontal(heading);
    const auto lead_ticks = static_cast<std::uint32_t>(std::lround(lead * ticks_per_second));
    const std::uint32_t start = centre_tick > lead_ticks ? centre_tick - lead_ticks : 0;
    const double3 entry = over - heading * (speed * (centre_tick - start) * tick_seconds);
    FlightBuilder builder(std::move(name), start, entry, heading * speed);
    builder.cruise(2 * lead_ticks);
    builder.unlist_at(start + 2 * lead_ticks);
    return builder.build();
}
}
