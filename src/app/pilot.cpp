#include "pilot.hpp"
#include "../graphics/particle_layout.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace pyroclast
{
namespace
{
template <typename Control>
const Control* state_at(const std::vector<Control>& tape, std::uint32_t tick)
{
    const auto found = std::find_if(tape.begin(), tape.end(), [tick](const Control& control) { return control.knot.tick == tick; });
    return found == tape.end() ? nullptr : &*found;
}

FlightKnot knot_at(const FlightPlan& plan, std::uint32_t tick)
{
    const auto state = flight_state(plan, tick);
    FlightKnot knot;
    knot.tick = tick;
    knot.position = state.position;
    knot.velocity = state.velocity;
    knot.acceleration = state.acceleration;
    knot.heading = state.heading;
    return knot;
}

double surface_ahead(const Environment& environment, bool props, const FlightKnot& knot, double seconds)
{
    const double3 ahead = knot.position + knot.velocity * seconds;
    return std::max(surface_below(environment, props, knot.position.x, knot.position.z), surface_below(environment, props, ahead.x, ahead.z));
}
}

double surface_below(const Environment& environment, bool props, double x, double z)
{
    const float fx = static_cast<float>(x);
    const float fz = static_cast<float>(z);
    const float ground = environment.terrain().height(fx, fz);
    if (!props) return ground;
    const auto hit = environment.raycast({{fx, ground + 60, fz}, {0, -1, 0}}, 61, true);
    return hit ? std::max(ground, hit->point.y) : ground;
}

void Pilot::pilot_helicopter(const FlightKnot& knot, const Camera& view, const JetOrbit& orbit)
{
    if (jet_ == JetPiloting::piloted)
    {
        jet_ = JetPiloting::circling;
        orbit_ = orbit;
    }
    helicopter_ = HelicopterPiloting::piloted;
    helicopter_home_ = false;
    helicopter_tape_.assign(1, HelicopterControl{knot});
    chase_.follow_helicopter(view);
}

void Pilot::pilot_jet(const JetControl& control)
{
    if (helicopter_ == HelicopterPiloting::piloted) helicopter_ = HelicopterPiloting::settling;
    jet_ = JetPiloting::piloted;
    jet_home_ = false;
    jet_tape_.assign(1, control);
    chase_.follow_jet();
}

void Pilot::release(const JetOrbit& orbit)
{
    if (helicopter_ == HelicopterPiloting::piloted) helicopter_ = HelicopterPiloting::settling;
    if (jet_ == JetPiloting::piloted)
    {
        jet_ = JetPiloting::circling;
        orbit_ = orbit;
    }
}

void Pilot::drop_jet()
{
    jet_ = JetPiloting::none;
    jet_home_ = false;
    jet_tape_.clear();
}

bool Pilot::fly_home()
{
    helicopter_home_ = helicopter_ == HelicopterPiloting::piloted;
    jet_home_ = jet_ == JetPiloting::piloted;
    return returning();
}

void Pilot::reset()
{
    helicopter_ = HelicopterPiloting::none;
    jet_ = JetPiloting::none;
    helicopter_home_ = false;
    jet_home_ = false;
    helicopter_tape_.clear();
    jet_tape_.clear();
}

bool Pilot::advance(std::uint32_t tick, const CameraInput& input, float view_heading, const PilotScene& scene, Rotorcraft& rotorcraft,
    Jetcraft& jetcraft, const Environment& environment, bool props)
{
    scene_ = scene;
    view_heading_ = view_heading;
    if (input.move.x != 0 || input.move.y != 0 || input.move.z != 0)
    {
        helicopter_home_ = false;
        jet_home_ = false;
    }
    if (helicopter_ != HelicopterPiloting::piloted) helicopter_home_ = false;
    if (jet_ != JetPiloting::piloted) jet_home_ = false;
    bool arrived = false;
    constexpr auto steps = particle_layout::maximum_simulation_steps + 1;
    std::vector<FlightKnot> knots;
    if (helicopter_ != HelicopterPiloting::none && !rotorcraft.helicopter()) helicopter_ = HelicopterPiloting::none;
    if (helicopter_ != HelicopterPiloting::none)
    {
        HelicopterControl start;
        if (const auto* found = state_at(helicopter_tape_, tick)) start = *found;
        else start.knot = knot_at(rotorcraft.helicopter()->plan, tick);
        helicopter_surface_ = surface_below(environment, props, start.knot.position.x, start.knot.position.z);
        if (helicopter_home_ && std::hypot(start.knot.position.x - scene.x, start.knot.position.z - scene.z) < 5 &&
            std::abs(start.knot.position.y - scene.surface - pilot::home_height) < 2 && length(start.knot.velocity) < 1)
        {
            helicopter_home_ = false;
            arrived = true;
        }
        if (helicopter_ == HelicopterPiloting::settling && at_rest(start))
        {
            FlightKnot rest = start.knot;
            rest.tick = tick + 1;
            rest.velocity = {};
            rest.acceleration = {};
            rest.yaw_rate = 0;
            rotorcraft.steer(tick, std::span(&rest, 1));
            helicopter_ = HelicopterPiloting::none;
            helicopter_tape_.clear();
        }
        else
        {
            HelicopterInput keys;
            keys.scene = scene;
            keys.home = helicopter_home_;
            if (helicopter_ == HelicopterPiloting::piloted)
            {
                keys.right = input.move.x;
                keys.up = input.move.y;
                keys.forward = input.move.z;
                keys.fast = input.fast;
                keys.view_heading = view_heading;
            }
            helicopter_tape_.assign(1, start);
            for (std::uint32_t index = 0; index < steps; ++index)
            {
                const auto& last = helicopter_tape_.back();
                helicopter_tape_.push_back(step(last, keys, surface_ahead(environment, props, last.knot, 1.5), settings_.helicopter));
                knots.push_back(helicopter_tape_.back().knot);
            }
            rotorcraft.steer(tick, knots);
        }
    }
    if (jet_ != JetPiloting::none && (!jetcraft.flight() || jetcraft.flight()->passes.empty())) jet_ = JetPiloting::none;
    if (jet_ != JetPiloting::none)
    {
        JetControl start;
        if (const auto* found = state_at(jet_tape_, tick)) start = *found;
        else start = jet_control(knot_at(jetcraft.flight()->passes.back(), tick));
        jet_surface_ = surface_below(environment, props, start.knot.position.x, start.knot.position.z);
        if (jet_home_ && std::hypot(start.knot.position.x - scene.x, start.knot.position.z - scene.z) < pilot::jet_home_reach)
        {
            jet_home_ = false;
            arrived = true;
        }
        JetInput keys;
        keys.scene = scene;
        keys.home = jet_home_;
        if (jet_ == JetPiloting::piloted)
        {
            keys.roll = input.move.x;
            keys.pitch = -input.move.z;
            keys.throttle = input.move.y;
            keys.afterburner = input.fast;
        }
        jet_tape_.assign(1, start);
        knots.clear();
        for (std::uint32_t index = 0; index < steps; ++index)
        {
            const auto& last = jet_tape_.back();
            const double surface = surface_ahead(environment, props, last.knot, 1.0);
            jet_tape_.push_back(jet_ == JetPiloting::piloted ? step(last, keys, surface, settings_.jet) : orbit_step(last, orbit_, surface, settings_.jet));
            knots.push_back(jet_tape_.back().knot);
        }
        jetcraft.steer(tick, knots);
    }
    return arrived;
}

std::optional<PilotReadout> Pilot::readout(std::uint32_t tick) const
{
    PilotReadout shown;
    shown.chase = chase_.attached();
    const auto locate = [this, &shown](double3 position, double heading) {
        const double dx = scene_.x - position.x;
        const double dz = scene_.z - position.z;
        shown.scene_distance = std::hypot(dx, dz);
        shown.scene_bearing = std::remainder(std::atan2(dz, dx) - heading, 2 * rotor::pi) * 180 / rotor::pi;
    };
    shown.returning = returning();
    if (helicopter_ == HelicopterPiloting::piloted && !helicopter_tape_.empty())
    {
        const auto* found = state_at(helicopter_tape_, tick);
        const auto& knot = (found ? *found : helicopter_tape_.front()).knot;
        shown.aircraft = "helicopter";
        shown.speed = length(knot.velocity);
        shown.height = knot.position.y - rotor::hub_above_skids - helicopter_surface_;
        locate(knot.position, view_heading_);
        return shown;
    }
    if (jet_ == JetPiloting::piloted && !jet_tape_.empty())
    {
        const auto* found = state_at(jet_tape_, tick);
        const auto& control = found ? *found : jet_tape_.front();
        shown.aircraft = "jet";
        shown.speed = length(control.knot.velocity);
        shown.height = control.knot.position.y - jet_surface_;
        shown.load = control.load;
        shown.bank = control.bank * 180 / rotor::pi;
        shown.mach = shown.speed / jet_layout::jet_sound_speed;
        locate(control.knot.position, std::atan2(control.knot.velocity.z, control.knot.velocity.x));
        return shown;
    }
    return std::nullopt;
}
}
