#pragma once

#include "rotorcraft.hpp"
#include "../graphics/jet_layout.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// Pure functions of absolute ticks (the passes' plans, wind, terrain), so a frozen frame equals the scenario's.
namespace pyroclast
{
// Kept in step with jet_layout.hlsli (a test checks they agree).
namespace jet
{
inline constexpr double pi = rotor::pi;
inline constexpr double gravity = rotor::gravity;
inline constexpr double density = rotor::density;
inline constexpr double mass = 12000;
inline constexpr double span = 10;
inline constexpr double spacing = pi / 4 * span;
inline constexpr double drag_area = 1.3;
inline constexpr double efficiency = 0.8;
inline constexpr double nozzle_aft = 7.4;
inline constexpr double nozzle_radius = 0.45;
inline constexpr double core_radius = 0.08 * span;
inline constexpr double core_viscosity = 0.05;
inline constexpr double decay_start = 8;
inline constexpr double decay_time = 10;
inline constexpr double knot_life = 40;
inline constexpr double image_far = 4 * spacing;
inline constexpr double image_near = 2 * spacing;
inline constexpr double mixing = 9;
inline constexpr double exhaust_time = 1.5;
inline constexpr std::uint32_t integration_ticks = 6;
inline constexpr double range_box = 500;
inline constexpr std::uint32_t taper_knots = 20;
inline constexpr double pass_speed = 160;
inline constexpr double fast_speed = 290;
inline constexpr double attack_per_g = 3 * pi / 180;
}
static_assert(jet::mass == jet_layout::jet_mass && jet::span == jet_layout::jet_span);

// Body frame: x forward, y up, z right; afterburner 0-1.
struct JetPose
{
    double3 centre;
    double3 forward;
    double3 up;
    double3 right;
    double3 velocity;
    double load = 1;
    double airspeed = 0;
    double thrust = 0;
    double mach = 0;
    double afterburner = 0;
    double fade = 0;
    bool listed = false;
};
double vapor_strength(const JetPose& pose, double humidity);

using JetPass = FlightPlan;

// Flown one after another: a pass's listing never overlaps the next's.
struct JetFlight
{
    std::string name;
    std::vector<JetPass> passes;
};
const JetPass* listed_pass(const JetFlight& flight, std::uint32_t tick);
// A push (lift below the plane across the path) keeps the airframe upright at a negative load.
JetPose jet_pose(const JetFlight& flight, std::uint32_t tick, const Terrain& terrain, const WindSettings& wind, bool gusts, std::uint32_t mask);

// exhaust is the exhaust's momentum per metre of track (N s/m); knots of different passes never join.
struct KnotBirth
{
    std::uint32_t tick = never;
    std::uint32_t pass = 0;
    double3 centre;
    double3 forward;
    double3 right;
    double ground = 0;
    double gamma = 0;
    double exhaust = 0;
};
std::optional<KnotBirth> knot_birth(const JetFlight& flight, std::uint32_t tick, const Terrain& terrain, const WindSettings& wind, bool gusts,
    std::uint32_t mask);

struct KnotState
{
    double height = 0;
    double half_spacing = 0;
    double3 drift;
    double gamma = 0;
    double core = 0;
    double exhaust_speed = 0;
    double exhaust_radius = 0;
    double3 left;
    double3 right;
    double3 centre;
};
struct PairRate
{
    double height = 0;
    double half_spacing = 0;
};
PairRate pair_rate(double height, double half_spacing, double gamma);
double knot_gamma(const KnotBirth& birth, double age);
double knot_core(double age);
double exhaust_radius(double age);
double exhaust_speed(double exhaust, double age);

// RK4 from each knot's birth, memoised, so a knot's state at a tick does not depend on how ticks fell into frames.
class JetHistory final
{
public:
    static constexpr std::uint32_t length = 1024;
    KnotState state(const KnotBirth& birth, std::uint32_t age_ticks, const WindSettings& wind, bool gusts, std::uint32_t mask);
    // The pilot rewrites the plan after `tick`, and a skipped frame may have memoised a knot it did not commit.
    void forget_after(std::uint32_t tick);
    void clear();

private:
    struct Entry
    {
        std::uint32_t tick = never;
        std::uint32_t steps = 0;
        double height = 0;
        double half_spacing = 0;
        double3 drift;
    };
    std::array<Entry, length> entries_{};
};

// FrameConstants::jet_* for the GPU: knots newest first.
struct JetKnotRecord
{
    std::array<float4, 4> words{};
};
struct JetHead
{
    std::array<float4, 4> words{};
};
struct JetWake
{
    std::uint32_t count = 0;
    std::array<JetKnotRecord, jet_layout::jet_knot_limit> knots{};
    bool joined = false;
    float3 box_min;
    float3 box_max;
    bool empty = true;
};
JetWake jet_wake(const JetFlight& flight, std::uint32_t tick, const Terrain& terrain, const WindSettings& wind, bool gusts, std::uint32_t mask,
    JetHistory& history);
JetHead jet_head(const JetPose& pose);
void include_head(JetWake& wake, const JetHead& head);

// jet_flow.hlsli's twin, without turbulence.
double3 jet_air(const JetWake& wake, const JetHead& head, double3 point);

// For FrameConstants::jet_births and jet_dust_lines: emission 0-1,024, half_width in metres.
struct JetEmission
{
    std::uint32_t dust = 0;
    float2 dust_from;
    float2 dust_to;
    float half_width = 0;
};
JetEmission jet_emission(const JetFlight& flight, std::uint32_t tick, const Terrain& terrain, const WindSettings& wind, bool gusts, std::uint32_t mask,
    JetHistory& history);

// jet_compute.hlsli's twin, for record `local` (0-2,559) of a jet slot seeded `seed`.
DustCycle jet_cycle(std::uint32_t seed, std::uint32_t local);
bool jet_born(std::uint32_t seed, std::uint32_t local, std::uint32_t cycle, std::uint32_t emission);

// A memo of each step tick's emissions (as RotorHistory); 721 ticks cover every live cycle.
class JetEmissionHistory final
{
public:
    static constexpr std::uint32_t length = 721;
    struct Entry
    {
        std::uint32_t tick = never;
        std::uint32_t dust = 0;
        float3 point;
    };
    const Entry* find(std::uint32_t tick) const;
    void store(std::uint32_t tick, const JetEmission& emission, float3 point);
    void clear();

private:
    std::array<Entry, length> entries_{};
};

// The GPU's twin: the dust alive after `tick` (none while its layer is hidden) and its mean birth point.
struct JetCensus
{
    std::uint32_t dust = 0;
    double3 mean_point;
};
JetCensus jet_census(const JetEmissionHistory& history, std::uint32_t seed, std::uint32_t start_tick, std::uint32_t end_tick, std::uint32_t tick,
    bool dust_drawn);

// The aircraft shader variants latch as the helicopter's.
class Jetcraft final
{
public:
    const std::optional<JetFlight>& flight() const { return flight_; }
    bool variants() const { return variants_; }
    void fly(JetFlight flight, std::uint32_t seed);
    void add_pass(JetPass pass, std::uint32_t seed);
    bool take_over(std::uint32_t tick);
    void steer(std::uint32_t tick, std::span<const FlightKnot> knots);
    // Returns the tick a new pass may be listed from.
    std::uint32_t end_passes(std::uint32_t tick, std::uint32_t fade_ticks);
    void clear();
    JetHistory& history() { return history_; }
    JetEmissionHistory& emissions() { return emissions_; }
    const JetEmissionHistory& emissions() const { return emissions_; }
    std::uint32_t seed() const { return seed_; }
    bool request_pending() const { return request_pending_; }
    void requested() { request_pending_ = false; }
    std::uint32_t first_list_tick() const;
    std::uint32_t slot_end() const;

private:
    std::optional<JetFlight> flight_;
    bool variants_ = false;
    bool request_pending_ = false;
    std::uint32_t seed_ = 0;
    JetHistory history_;
    JetEmissionHistory emissions_;
};

JetPass straight_pass(std::string name, double3 over, double3 heading, double speed, std::uint32_t centre_tick, double lead);
}
