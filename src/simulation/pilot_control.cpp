#include "pilot_control.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace pyroclast
{
namespace
{
constexpr double tick_seconds = 1.0 / 60.0;
constexpr double degree = rotor::pi / 180;
const double3 world_up{0, 1, 0};

double saturate(double x)
{
    return std::clamp(x, 0.0, 1.0);
}

double smoothstep(double edge0, double edge1, double x)
{
    const double t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3 - 2 * t);
}

double wrap_angle(double angle)
{
    return std::remainder(angle, 2 * rotor::pi);
}

double3 clamp_length(double3 a, double limit)
{
    const double size = length(a);
    return size > limit ? a * (limit / size) : a;
}

double3 horizontal_direction(double3 a)
{
    const double3 flat{a.x, 0, a.z};
    return length(flat) > 1e-9 ? normalize(flat) : double3{1, 0, 0};
}

constexpr std::array<PilotSettingInfo, pilot_setting_count> setting_infos{{
    {"Speed", "%.0f m/s", 5, 60},
    {"Speed with Shift", "%.0f m/s", 10, 80},
    {"Climb rate", "%.1f m/s", 1, 15},
    {"Acceleration", "%.1f m/s^2", 2, 15},
    {"Turn rate", "%.0f deg/s", 20, 180},
    {"Boundary", "%.0f m", 100, 500},
    {"Cruise speed", "%.0f m/s", 60, 290},
    {"Top speed (Shift)", "%.0f m/s", 80, 290},
    {"Bank", "%.0f deg", 30, 85},
    {"Highest load", "%.1f g", 2, 9},
    {"Pull (S)", "%.1f g", 1, 8},
    {"Roll rate", "%.0f deg/s", 60, 360},
    {"Boundary", "%.0f m", 200, 800},
}};

template <typename Settings>
auto* setting_field(Settings& settings, PilotSetting setting)
{
    auto& helicopter = settings.helicopter;
    auto& jet = settings.jet;
    switch (setting)
    {
    case PilotSetting::helicopter_speed: return &helicopter.speed;
    case PilotSetting::helicopter_fast_speed: return &helicopter.fast_speed;
    case PilotSetting::helicopter_climb_rate: return &helicopter.climb_rate;
    case PilotSetting::helicopter_acceleration: return &helicopter.acceleration;
    case PilotSetting::helicopter_turn_rate: return &helicopter.turn_rate;
    case PilotSetting::helicopter_boundary: return &helicopter.boundary;
    case PilotSetting::jet_cruise_speed: return &jet.cruise_speed;
    case PilotSetting::jet_top_speed: return &jet.top_speed;
    case PilotSetting::jet_bank: return &jet.bank;
    case PilotSetting::jet_highest_load: return &jet.highest_load;
    case PilotSetting::jet_pull: return &jet.pull;
    case PilotSetting::jet_roll_rate: return &jet.roll_rate;
    case PilotSetting::jet_boundary: return &jet.boundary;
    case PilotSetting::count: break;
    }
    return static_cast<decltype(&helicopter.speed)>(nullptr);
}
}

const PilotSettingInfo& pilot_setting_info(PilotSetting setting)
{
    return setting_infos[std::min(static_cast<std::uint32_t>(setting), pilot_setting_count - 1)];
}

double pilot_setting(const PilotSettings& settings, PilotSetting setting)
{
    const double* field = setting_field(settings, setting);
    return field ? *field : 0;
}

bool set_pilot_setting(PilotSettings& settings, PilotSetting setting, double value)
{
    double* field = setting_field(settings, setting);
    if (!field || !std::isfinite(value)) return false;
    const auto& info = pilot_setting_info(setting);
    *field = std::clamp(value, info.minimum, info.maximum);
    return true;
}

HelicopterControl step(const HelicopterControl& control, const HelicopterInput& input, double surface, const HelicopterSettings& settings)
{
    constexpr double dt = tick_seconds;
    const auto& knot = control.knot;
    const double climb = input.fast ? settings.climb_rate * 10 / 6 : settings.climb_rate;
    const double lowest = surface + rotor::hub_above_skids + pilot::skid_clearance;
    double3 command;
    if (input.home)
    {
        const double3 to{input.scene.x - knot.position.x, 0, input.scene.z - knot.position.z};
        const double distance = length(to);
        const double speed = std::min(settings.fast_speed, std::sqrt(2 * pilot::home_braking * distance));
        command = distance > 1e-6 ? to * (speed / distance) : double3{};
        command.y = 0.5 * (input.scene.surface + pilot::home_height - knot.position.y);
    }
    else
    {
        const double3 ahead{std::cos(input.view_heading), 0, std::sin(input.view_heading)};
        const double3 right{-ahead.z, 0, ahead.x};
        const double3 wish = clamp_length(ahead * std::clamp(input.forward, -1.0, 1.0) + right * std::clamp(input.right, -1.0, 1.0), 1);
        command = wish * (input.fast ? settings.fast_speed : settings.speed);
        command.y = std::clamp(input.up, -1.0, 1.0) * climb;
    }
    command.y = std::max(command.y, -0.5 * (knot.position.y - lowest));
    command.y = std::min(command.y, 0.5 * (surface + pilot::helicopter_ceiling - knot.position.y));
    command.y = std::clamp(command.y, -climb, climb);
    const double3 offset{knot.position.x - input.scene.x, 0, knot.position.z - input.scene.z};
    const double radius = length(offset);
    if (radius > 1e-6)
    {
        const double3 outwards = offset * (1 / radius);
        const double room = settings.boundary - radius - std::max(dot(knot.velocity, outwards), 0.0) * pilot::velocity_time;
        const double limit = room >= 0 ? std::sqrt(2 * pilot::boundary_braking * room) : 0.5 * room;
        const double along = dot(command, outwards);
        if (along > limit) command = command + outwards * (limit - along);
    }
    const double3 wanted = clamp_length((command - knot.velocity) * (1 / pilot::velocity_time), settings.acceleration);
    const double3 change = clamp_length((wanted - knot.acceleration) * (dt / pilot::acceleration_lag), 5 * settings.acceleration * dt);
    HelicopterControl next;
    auto& out = next.knot;
    out.tick = knot.tick + 1;
    out.acceleration = knot.acceleration + change;
    out.velocity = knot.velocity + out.acceleration * dt;
    out.position = knot.position + (knot.velocity + out.velocity) * (0.5 * dt);
    if (out.position.y < lowest)
    {
        out.position.y = lowest;
        out.velocity.y = std::max(out.velocity.y, 0.0);
        out.acceleration.y = std::max(out.acceleration.y, 0.0);
    }
    const double turn_rate = settings.turn_rate * degree;
    const double track = std::hypot(out.velocity.x, out.velocity.z);
    const double turn = track > 1e-6 ? wrap_angle(std::atan2(out.velocity.z, out.velocity.x) - knot.heading) : 0;
    const double rate = smoothstep(2, 5, track) * std::clamp(pilot::yaw_gain * turn, -turn_rate, turn_rate);
    out.yaw_rate = knot.yaw_rate + std::clamp(rate - knot.yaw_rate, -4 * turn_rate * dt, 4 * turn_rate * dt);
    out.heading = wrap_angle(knot.heading + out.yaw_rate * dt);
    return next;
}

bool at_rest(const HelicopterControl& control)
{
    const auto& knot = control.knot;
    return length(knot.velocity) < 0.02 && length(knot.acceleration) < 0.02 && std::abs(knot.yaw_rate) < 1e-3;
}

namespace
{
struct PathFrame
{
    double3 forward;
    double3 right;
    double3 up;
    double path = 0;
};

PathFrame path_frame(double3 velocity)
{
    PathFrame frame;
    frame.forward = length(velocity) > 1e-6 ? normalize(velocity) : double3{1, 0, 0};
    frame.right = normalize(cross(frame.forward, world_up));
    if (length(frame.right) < 0.5) frame.right = {0, 0, 1};
    frame.up = cross(frame.right, frame.forward);
    frame.path = std::asin(std::clamp(frame.forward.y, -1.0, 1.0));
    return frame;
}

double level_load(double path, double bank)
{
    return std::cos(path) / std::max(std::cos(bank), std::cos(pilot::jet_bank_limit));
}

double usable_bank(const JetSettings& settings)
{
    return std::min({settings.bank * degree, std::acos(1 / std::max(settings.highest_load, 1.0)), pilot::jet_bank_limit});
}

struct JetCommand
{
    double bank = 0;
    double load = 1;
    double speed = JetSettings{}.cruise_speed;
};

JetControl advance(const JetControl& control, JetCommand command, double surface, const JetSettings& settings)
{
    constexpr double dt = tick_seconds;
    constexpr double g = rotor::gravity;
    const auto& knot = control.knot;
    const auto frame = path_frame(knot.velocity);
    const double speed = length(knot.velocity);
    const double level = level_load(frame.path, control.bank);
    if (frame.path > pilot::jet_path_limit) command.load = std::min(command.load, level - (frame.path - pilot::jet_path_limit) / (5 * degree));
    if (frame.path < -pilot::jet_path_limit) command.load = std::max(command.load, level + (-pilot::jet_path_limit - frame.path) / (5 * degree));
    const double height = knot.position.y - surface;
    const double pull_radius = speed * speed / (g * pilot::jet_guard_pull);
    const double reaction = pilot::jet_guard_reaction + std::max(1 + pilot::jet_guard_pull - control.load, 0.0) / pilot::jet_load_rate;
    const double safe = pilot::jet_floor + reaction * std::max(-knot.velocity.y, 0.0) +
        (frame.path < 0 ? pull_radius * (1 - std::cos(frame.path)) : 0);
    if (height < safe)
    {
        command.bank = std::clamp(command.bank, -pilot::jet_guard_bank, pilot::jet_guard_bank);
        command.load = std::max(command.load, level + pilot::jet_guard_pull * saturate((safe - height) / 5));
    }
    if (height > pilot::jet_ceiling) command.load = std::min(command.load, level - std::min((height - pilot::jet_ceiling) / 50, 1.0));
    command.load = std::clamp(command.load, pilot::jet_lowest_load, settings.highest_load);
    command.bank = std::clamp(command.bank, -pilot::jet_bank_limit, pilot::jet_bank_limit);
    command.speed = std::clamp(command.speed, pilot::jet_slowest, std::max(settings.top_speed, pilot::jet_slowest));
    JetControl next = control;
    const double roll_rate = settings.roll_rate * degree;
    next.bank = control.bank + std::clamp((command.bank - control.bank) * (dt / pilot::jet_roll_lag), -roll_rate * dt, roll_rate * dt);
    next.load = control.load + std::clamp(command.load - control.load, -pilot::jet_load_rate * dt, pilot::jet_load_rate * dt);
    const double wanted = std::clamp((command.speed - speed) / pilot::jet_speed_time, -pilot::jet_tangential_limit, pilot::jet_tangential_limit);
    next.tangential = control.tangential + std::clamp(wanted - control.tangential, -pilot::jet_tangential_limit * dt, pilot::jet_tangential_limit * dt);
    const double3 lift_axis = frame.up * std::cos(next.bank) + frame.right * std::sin(next.bank);
    const double3 gravity_across = (world_up - frame.forward * frame.forward.y) * g;
    auto& out = next.knot;
    out.tick = knot.tick + 1;
    out.acceleration = frame.forward * next.tangential + lift_axis * (next.load * g) - gravity_across;
    out.velocity = knot.velocity + out.acceleration * dt;
    out.position = knot.position + (knot.velocity + out.velocity) * (0.5 * dt);
    if (out.position.y < surface + pilot::jet_hard_floor)
    {
        out.position.y = surface + pilot::jet_hard_floor;
        out.velocity.y = std::max(out.velocity.y, 0.0);
    }
    out.heading = std::atan2(out.velocity.z, out.velocity.x);
    out.yaw_rate = wrap_angle(out.heading - knot.heading) / dt;
    return next;
}

double heading_error(const FlightKnot& knot, double3 desired)
{
    const double3 track = horizontal_direction(knot.velocity);
    const double3 right{-track.z, 0, track.x};
    return std::atan2(dot(desired, right), dot(desired, track));
}

double home_bank(const JetControl& control, const PilotScene& scene, const JetSettings& settings)
{
    const auto& knot = control.knot;
    const double3 towards = horizontal_direction({scene.x - knot.position.x, 0, scene.z - knot.position.z});
    double error = heading_error(knot, towards);
    if (std::abs(error) > 150 * degree) error = (control.bank >= 0 ? 1 : -1) * std::abs(error);
    const double bank = usable_bank(settings);
    return std::clamp(2 * error, -bank, bank);
}

double holding_load(const FlightKnot& knot, double bank, double height)
{
    return level_load(path_frame(knot.velocity).path, bank) + std::clamp(0.03 * (height - knot.position.y) - 0.1 * knot.velocity.y, -1.0, 2.0);
}
}

JetControl jet_control(const FlightKnot& knot)
{
    JetControl control;
    control.knot = knot;
    const auto frame = path_frame(knot.velocity);
    double3 lift = knot.acceleration + world_up * rotor::gravity;
    lift = lift - frame.forward * dot(lift, frame.forward);
    const double sense = dot(lift, frame.up) < 0 ? -1.0 : 1.0;
    control.load = sense * length(lift) / rotor::gravity;
    const double3 up = lift * sense;
    control.bank = length(lift) > 1e-6 ? std::atan2(dot(up, frame.right), dot(up, frame.up)) : 0;
    control.tangential = dot(knot.acceleration, frame.forward);
    control.speed_target = std::max(length(knot.velocity), pilot::jet_slowest);
    return control;
}

JetControl step(const JetControl& control, const JetInput& input, double surface, const JetSettings& settings)
{
    JetControl throttled = control;
    throttled.speed_target = std::clamp(control.speed_target + std::clamp(input.throttle, -1.0, 1.0) * pilot::jet_throttle_rate * tick_seconds,
        pilot::jet_slowest, std::max(settings.top_speed, pilot::jet_slowest));
    const auto frame = path_frame(control.knot.velocity);
    const double pitch = std::clamp(input.pitch, -1.0, 1.0);
    JetCommand command;
    const auto& knot = control.knot;
    if (input.home)
    {
        command.bank = home_bank(control, input.scene, settings);
        command.load = holding_load(knot, control.bank, input.scene.surface + pilot::jet_home_height);
        command.speed = settings.cruise_speed;
        return advance(throttled, command, surface, settings);
    }
    command.bank = std::clamp(input.roll, -1.0, 1.0) * usable_bank(settings);
    const double levelling = std::clamp(-frame.path * pilot::jet_level_gain, -pilot::jet_level_limit, pilot::jet_level_limit);
    command.load = level_load(frame.path, control.bank) + (pitch > 0 ? pitch * settings.pull : pitch < 0 ? pitch * pilot::jet_push : levelling);
    command.speed = input.afterburner ? settings.top_speed : throttled.speed_target;
    const double3 ahead = knot.position + knot.velocity * pilot::jet_boundary_lead;
    const double3 towards = horizontal_direction({input.scene.x - knot.position.x, 0, input.scene.z - knot.position.z});
    const double beyond = smoothstep(settings.boundary, settings.boundary + pilot::jet_boundary_blend,
                              std::hypot(ahead.x - input.scene.x, ahead.z - input.scene.z)) *
        (1 - smoothstep(0.6, 0.9, dot(horizontal_direction(knot.velocity), towards)));
    if (beyond > 0)
    {
        command.bank = (1 - beyond) * command.bank + beyond * home_bank(control, input.scene, settings);
        command.load = (1 - beyond) * command.load + beyond * (level_load(frame.path, control.bank) + levelling);
        command.speed = (1 - beyond) * command.speed + beyond * settings.cruise_speed;
    }
    return advance(throttled, command, surface, settings);
}

JetControl orbit_step(const JetControl& control, const JetOrbit& orbit, double surface, const JetSettings& settings)
{
    const auto& knot = control.knot;
    const double3 here{knot.position.x - orbit.centre_x, 0, knot.position.z - orbit.centre_z};
    const double distance = length(here);
    const double3 out = distance > 1e-6 ? here * (1 / distance) : double3{1, 0, 0};
    const double3 track = horizontal_direction(knot.velocity);
    const double3 right{-track.z, 0, track.x};
    double3 tangent{-out.z, 0, out.x};
    if (dot(tangent, track) < 0) tangent = tangent * -1.0;
    const double error = distance - pilot::orbit_radius;
    const double inward = std::clamp(std::atan(error / 150), -60 * degree, 60 * degree);
    const double turn = heading_error(knot, tangent * std::cos(inward) - out * std::sin(inward));
    const double side = dot(out, right) < 0 ? 1 : -1;
    const double speed = length(knot.velocity);
    const double circle = side * std::atan(speed * speed / (rotor::gravity * pilot::orbit_radius)) * saturate(1 - std::abs(error) / pilot::orbit_radius);
    JetCommand command;
    command.bank = circle + 1.5 * turn;
    command.load = holding_load(knot, control.bank, orbit.height);
    command.speed = pilot::orbit_speed;
    return advance(control, command, surface, settings);
}
}
