#pragma once

#include "camera.hpp"
#include "chase_camera.hpp"
#include "../simulation/environment.hpp"
#include "../simulation/jet.hpp"
#include "../simulation/pilot_control.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace pyroclast
{
enum class HelicopterPiloting
{
    none,
    piloted,
    settling,
};
enum class JetPiloting
{
    none,
    piloted,
    circling,
};

// Angles in degrees; the scene's bearing is positive to the right of the aircraft's heading.
struct PilotReadout
{
    std::string_view aircraft;
    double speed = 0;
    double height = 0;
    double load = 0;
    double bank = 0;
    double mach = 0;
    bool chase = true;
    double scene_distance = 0;
    double scene_bearing = 0;
    bool returning = false;
};

// Ticks a frame did not commit are rewritten by the next, so a committed tick never changes and the CPU twins
// stay exact.
class Pilot final
{
public:
    HelicopterPiloting helicopter() const { return helicopter_; }
    JetPiloting jet() const { return jet_; }
    bool controlling() const { return helicopter_ == HelicopterPiloting::piloted || jet_ == JetPiloting::piloted; }
    ChaseCamera& chase() { return chase_; }
    const ChaseCamera& chase() const { return chase_; }
    PilotSettings& settings() { return settings_; }
    const PilotSettings& settings() const { return settings_; }

    void pilot_helicopter(const FlightKnot& knot, const Camera& view, const JetOrbit& orbit);
    void pilot_jet(const JetControl& control);
    void release(const JetOrbit& orbit);
    bool fly_home();
    bool returning() const { return helicopter_home_ || jet_home_; }
    void drop_jet();
    void reset();
    // Returns whether an aircraft flying home arrived.
    bool advance(std::uint32_t tick, const CameraInput& input, float view_heading, const PilotScene& scene, Rotorcraft& rotorcraft,
        Jetcraft& jetcraft, const Environment& environment, bool props);
    std::optional<PilotReadout> readout(std::uint32_t tick) const;

private:
    HelicopterPiloting helicopter_ = HelicopterPiloting::none;
    JetPiloting jet_ = JetPiloting::none;
    std::vector<HelicopterControl> helicopter_tape_;
    std::vector<JetControl> jet_tape_;
    JetOrbit orbit_;
    bool helicopter_home_ = false;
    bool jet_home_ = false;
    PilotScene scene_;
    float view_heading_ = 0;
    double helicopter_surface_ = 0;
    double jet_surface_ = 0;
    ChaseCamera chase_;
    PilotSettings settings_;
};

double surface_below(const Environment& environment, bool props, double x, double z);
}
