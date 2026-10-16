#pragma once

#include "terrain.hpp"
#include "wind.hpp"
#include "../core/math.hpp"
#include "../graphics/rotor_layout.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Pure functions of absolute ticks (plan, wind, terrain), so a frozen frame equals the scenario's.
namespace pyroclast
{
struct double3
{
    double x = 0;
    double y = 0;
    double z = 0;
};
constexpr double3 operator+(double3 a, double3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr double3 operator-(double3 a, double3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr double3 operator*(double3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
constexpr double3 operator*(double s, double3 a) { return a * s; }
constexpr double dot(double3 a, double3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr double3 cross(double3 a, double3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double length(double3 a);
double3 normalize(double3 a);
constexpr float3 to_float3(double3 a) { return {static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z)}; }
constexpr double3 to_double3(float3 a) { return {a.x, a.y, a.z}; }

// The first four are kept in step with rotor_layout.hlsli (rotorcraft_tests checks them).
namespace rotor
{
inline constexpr double pi = 3.14159265358979323846;
inline constexpr double radius = 8;
inline constexpr double mass = 10000;
inline constexpr double density = 1.225;
inline constexpr double gravity = 9.81;
inline constexpr double thrust = mass * gravity;
inline constexpr double area = pi * radius * radius;
inline constexpr double hover_inflow = 14.111934049548733;
inline constexpr double drag_area = 2.8;
inline constexpr double hub_above_skids = 4.3;
inline constexpr double tail_hub_aft = 9.88;
inline constexpr double tail_hub_below = 2.0;
inline constexpr double tail_disc_side = 0.45;
inline constexpr double largest_thrust_share = 1.1;
inline constexpr double cruise_speed = 40;
inline constexpr double acceleration_limit = 3.5;
inline constexpr std::uint32_t jerk_ticks = 60;
inline constexpr double descent_rate = 3;
inline constexpr double landing_rate = 1.5;
inline constexpr double touchdown_rate = 0.3;
inline constexpr double saltation_threshold = 6;
inline constexpr double emission_reach = 10;
}

// As lingering_wind in realism.hlsli blows it; without the mask's shear bit, only the ramp below 15 m.
double3 lingering_wind_at(const WindSettings& wind, std::uint32_t tick, bool gusts, double height, std::uint32_t mask);

// Heading in radians from +x towards +z.
struct FlightKnot
{
    std::uint32_t tick = 0;
    double3 position;
    double3 velocity;
    double3 acceleration;
    double heading = 0;
    double yaw_rate = 0;
};

struct FlightState
{
    double3 position;
    double3 velocity;
    double3 acceleration;
    double heading = 0;
};

inline constexpr std::uint32_t never = 0xffffffffu;

// Knots at absolute ticks joined by quintic Hermite segments; listed (drawn, air on) from list_tick until unlist_tick.
struct FlightPlan
{
    std::string name;
    std::vector<FlightKnot> knots;
    std::uint32_t list_tick = 0;
    std::uint32_t unlist_tick = never;
    std::uint32_t touchdown_tick = never;
    std::uint32_t spin_down_tick = never;
};
FlightState flight_state(const FlightPlan& plan, std::uint32_t tick);
// Covers the jet wake's knot_life (2,400 ticks).
inline constexpr std::uint32_t plan_memory_ticks = 2520;
// Keeps `plan` up to `tick` (every tick up to it evaluates unchanged) and continues it with one knot per later tick.
void continue_plan(FlightPlan& plan, std::uint32_t tick, std::span<const FlightKnot> knots);
double collective_share(const FlightPlan& plan, std::uint32_t tick);
double spin_share(const FlightPlan& plan, std::uint32_t tick);
// Radians, reduced modulo a quarter turn (the blades' symmetry).
double rotor_phase(const FlightPlan& plan, std::uint32_t tick, double speed);

class FlightBuilder final
{
public:
    FlightBuilder(std::string name, std::uint32_t tick, double3 position, double3 velocity);
    explicit FlightBuilder(FlightPlan plan);
    std::uint32_t tick() const { return state_.tick; }
    double3 position() const { return state_.position; }
    double3 velocity() const { return state_.velocity; }
    void cruise(std::uint32_t ticks);
    void change_velocity(double3 target, double limit = rotor::acceleration_limit);
    void arc(double3 towards, double radians, double acceleration);
    void transit_to(double3 point);
    void turn_to(double heading);
    void hold(std::uint32_t ticks);
    void vertical_to(double height, double rate);
    void land(const Terrain& terrain, std::uint32_t idle_ticks);
    void depart(double3 direction, std::uint32_t ticks);
    void unlist_at(std::uint32_t tick);
    FlightPlan build() const;

private:
    void append(std::uint32_t ticks, double3 position, double3 velocity, double3 acceleration);
    FlightPlan plan_;
    FlightKnot state_;
};

// Body frame: x forward, y up along the thrust (a hover is nose-up), z right.
struct HelicopterPose
{
    double3 hub;
    double3 forward;
    double3 up;
    double3 right;
    double main_phase = 0;
    double tail_phase = 0;
    double spin = 1;
    double fade = 0;
    double skid_height = 0;
};
HelicopterPose helicopter_pose(const FlightPlan& plan, std::uint32_t tick, const Terrain& terrain, const WindSettings& wind, bool gusts,
    std::uint32_t mask);
double helicopter_fade(const FlightPlan& plan, std::uint32_t tick);

// Per step, as rotor_flow.hlsli reads it: speeds in m/s, jet_flow in m^3/s, emission 0-1,024, and radii_cm the
// radii (cm) at which the dust's ground flux reaches 0, 1/3, 2/3 and 1 of its total.
struct RotorStep
{
    bool listed = false;
    double3 hub;
    double inflow = 0;
    double skew_x = 0;
    double skew_z = 0;
    double height = 0;
    double ring = 0;
    double jet_flow = 0;
    double jet_radius = 0;
    double3 ground;
    std::uint32_t emission = 0;
    std::array<std::uint32_t, 4> radii_cm{};
    double thrust_share = 0;
    double effective_thrust = 0;
    double peak_outwash = 0;
    double raw_emission = 0;
};
RotorStep rotor_step(const FlightPlan& plan, std::uint32_t tick, const Terrain& terrain, const WindSettings& wind, bool gusts,
    std::uint32_t mask, double reference_emission);
// A 1 g hover with the hub 2 R over flat ground in still air: emission 1,024.
double reference_emission();

// rotor_flow.hlsli's twin, in the wake's sheared frame: s is the squared radius from its axis, z the depth below the hub.
struct FrameVelocity
{
    double radial = 0;
    double vertical = 0;
};
FrameVelocity rotor_frame_velocity(const RotorStep& step, double s, double z, double y_ground);
double3 rotor_air(const RotorStep& step, double3 point, double terrain_height);
double rotor_stream(const RotorStep& step, double s, double z, double y_ground);

// rotor_compute.hlsli's twin, for record `local` (2-2,559) of a rotor slot seeded `seed`.
struct DustCycle
{
    std::uint32_t length = 0;
    std::uint32_t phase = 0;
};
DustCycle dust_cycle(std::uint32_t seed, std::uint32_t local);
bool dust_born(std::uint32_t seed, std::uint32_t local, std::uint32_t cycle, std::uint32_t emission);

// A memo of each tick's rotor state (a pure function of its tick); 1,081 ticks cover every live dust cycle.
class RotorHistory final
{
public:
    static constexpr std::uint32_t length = 1081;
    struct Entry
    {
        std::uint32_t tick = never;
        std::uint32_t emission = 0;
        std::uint32_t outer_cm = 0;
        float3 anchor;
    };
    const Entry* find(std::uint32_t tick) const;
    void store(std::uint32_t tick, const RotorStep& step, float3 drift);
    void clear();

private:
    std::array<Entry, length> entries_{};
};

// The GPU's twin: the dust alive after `tick` and its alive-weighted means.
struct DustCensus
{
    std::uint32_t alive = 0;
    double3 mean_point;
    double mean_outer = 0;
    double mean_age = 0;
};
DustCensus dust_census(const RotorHistory& history, std::uint32_t seed, std::uint32_t start_tick, std::uint32_t end_tick,
    std::uint32_t tick, float3 drift, bool layer_enabled);

struct Helicopter
{
    FlightPlan plan;
    std::uint32_t seed = 0;
    double reference = 0;
};

// The shader variants latch at fly() until clear(), so they never switch with rotor records on screen.
class Rotorcraft final
{
public:
    const std::optional<Helicopter>& helicopter() const { return helicopter_; }
    bool variants() const { return variants_; }
    void fly(FlightPlan plan, std::uint32_t seed);
    void replan(FlightPlan plan);
    void steer(std::uint32_t tick, std::span<const FlightKnot> knots);
    void clear();
    RotorHistory& history() { return history_; }
    const RotorHistory& history() const { return history_; }
    bool request_pending() const { return request_pending_; }
    void requested() { request_pending_ = false; }
    std::uint32_t slot_end() const;

private:
    std::optional<Helicopter> helicopter_;
    bool variants_ = false;
    bool request_pending_ = false;
    RotorHistory history_;
};
}
