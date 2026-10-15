#pragma once

#include "rotorcraft.hpp"

#include <cstdint>
#include <string_view>

namespace pyroclast
{
struct HelicopterSettings
{
    double speed = 25;
    double fast_speed = 45;
    double climb_rate = 6;
    double acceleration = 8;
    double turn_rate = 90;
    double boundary = 300;
};
struct JetSettings
{
    double cruise_speed = 100;
    double top_speed = 290;
    double bank = 82;
    double highest_load = 9;
    double pull = 6;
    double roll_rate = 240;
    double boundary = 400;
};
struct PilotSettings
{
    HelicopterSettings helicopter;
    JetSettings jet;
};

enum class PilotSetting : std::uint32_t
{
    helicopter_speed,
    helicopter_fast_speed,
    helicopter_climb_rate,
    helicopter_acceleration,
    helicopter_turn_rate,
    helicopter_boundary,
    jet_cruise_speed,
    jet_top_speed,
    jet_bank,
    jet_highest_load,
    jet_pull,
    jet_roll_rate,
    jet_boundary,
    count,
};
inline constexpr std::uint32_t pilot_setting_count = static_cast<std::uint32_t>(PilotSetting::count);
inline constexpr PilotSetting first_jet_setting = PilotSetting::jet_cruise_speed;
struct PilotSettingInfo
{
    std::string_view label;
    std::string_view format;
    double minimum = 0;
    double maximum = 0;
};
const PilotSettingInfo& pilot_setting_info(PilotSetting setting);
double pilot_setting(const PilotSettings& settings, PilotSetting setting);
bool set_pilot_setting(PilotSettings& settings, PilotSetting setting, double value);

namespace pilot
{
inline constexpr double velocity_time = 0.8;
inline constexpr double acceleration_lag = 0.2;
inline constexpr double skid_clearance = 0.3;
inline constexpr double helicopter_ceiling = 300;
inline constexpr double boundary_braking = 4;
inline constexpr double home_braking = 3;
inline constexpr double home_height = 20;
inline constexpr double yaw_gain = 3;

inline constexpr double jet_slowest = 60;
inline constexpr double jet_bank_limit = 85 * rotor::pi / 180;
inline constexpr double jet_roll_lag = 0.1;
inline constexpr double jet_lowest_load = -1.5;
inline constexpr double jet_push = 2.5;
inline constexpr double jet_load_rate = 10;
inline constexpr double jet_path_limit = 45 * rotor::pi / 180;
inline constexpr double jet_level_gain = 1 / (10 * rotor::pi / 180);
inline constexpr double jet_level_limit = 2;
inline constexpr double jet_floor = 15;
inline constexpr double jet_hard_floor = 3;
inline constexpr double jet_ceiling = 300;
inline constexpr double jet_guard_pull = 6;
inline constexpr double jet_guard_reaction = 0.3;
inline constexpr double jet_guard_bank = 45 * rotor::pi / 180;
inline constexpr double jet_speed_time = 1;
inline constexpr double jet_tangential_limit = 20;
inline constexpr double jet_throttle_rate = 60;
inline constexpr double jet_boundary_blend = 50;
inline constexpr double jet_boundary_lead = 3;
inline constexpr double jet_home_height = 60;
inline constexpr double jet_home_reach = 150;
inline constexpr double orbit_radius = 350;
inline constexpr double orbit_speed = 110;
inline constexpr double orbit_least_height = 30;
inline constexpr double orbit_greatest_height = 120;
}

struct PilotScene
{
    double x = 0;
    double z = 0;
    double surface = 0;
};

struct HelicopterInput
{
    double right = 0;
    double up = 0;
    double forward = 0;
    bool fast = false;
    double view_heading = 0;
    PilotScene scene;
    bool home = false;
};

struct HelicopterControl
{
    FlightKnot knot;
};
HelicopterControl step(const HelicopterControl& control, const HelicopterInput& input, double surface, const HelicopterSettings& settings = {});
bool at_rest(const HelicopterControl& control);

struct JetInput
{
    double roll = 0;
    double pitch = 0;
    double throttle = 0;
    bool afterburner = false;
    PilotScene scene;
    bool home = false;
};
struct JetControl
{
    FlightKnot knot;
    double bank = 0;
    double load = 1;
    double tangential = 0;
    double speed_target = JetSettings{}.cruise_speed;
};
JetControl jet_control(const FlightKnot& knot);
JetControl step(const JetControl& control, const JetInput& input, double surface, const JetSettings& settings = {});
struct JetOrbit
{
    double centre_x = 0;
    double centre_z = 0;
    double height = 0;
};
JetControl orbit_step(const JetControl& control, const JetOrbit& orbit, double surface, const JetSettings& settings = {});
}
