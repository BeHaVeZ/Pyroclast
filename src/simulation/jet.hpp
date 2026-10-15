#pragma once

#include "rotorcraft.hpp"
#include "../graphics/jet_layout.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace pyroclast
{
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
inline constexpr double pass_speed = 160;
inline constexpr double fast_speed = 290;
inline constexpr double attack_per_g = 3 * pi / 180;
}
static_assert(jet::mass == jet_layout::jet_mass && jet::span == jet_layout::jet_span);

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

struct JetFlight
{
    std::string name;
    std::vector<JetPass> passes;
};
const JetPass* listed_pass(const JetFlight& flight, std::uint32_t tick);
JetPose jet_pose(const JetFlight& flight, std::uint32_t tick, const Terrain& terrain, const WindSettings& wind, bool gusts, std::uint32_t mask);

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

class JetHistory final
{
public:
    static constexpr std::uint32_t length = 1024;
    KnotState state(const KnotBirth& birth, std::uint32_t age_ticks, const WindSettings& wind, bool gusts, std::uint32_t mask);
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

double3 jet_air(const JetWake& wake, const JetHead& head, double3 point);

class Jetcraft final
{
public:
    const std::optional<JetFlight>& flight() const { return flight_; }
    bool variants() const { return variants_; }
    void fly(JetFlight flight);
    void add_pass(JetPass pass);
    void clear();
    JetHistory& history() { return history_; }

private:
    std::optional<JetFlight> flight_;
    bool variants_ = false;
    JetHistory history_;
};

JetPass straight_pass(std::string name, double3 over, double3 heading, double speed, std::uint32_t centre_tick, double lead);
}
