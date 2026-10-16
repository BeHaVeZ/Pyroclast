#pragma once

#include "camera.hpp"
#include "../simulation/terrain.hpp"

#include <optional>

namespace pyroclast
{
// Radians from +x towards +z.
float view_heading(const Camera& view);

class ChaseCamera final
{
public:
    static constexpr float closest = 10;
    static constexpr float farthest = 250;

    void follow_helicopter(const Camera& view);
    void follow_jet();
    bool attached() const { return attached_; }
    void set_attached(bool attached) { attached_ = attached; }
    void orbit(float2 look);
    void zoom(float notches);
    float heading() const;
    Camera place(const Camera& view, float3 target, std::optional<float3> track, const Terrain& terrain) const;

private:
    float yaw_ = 0;
    float pitch_ = -0.3f;
    float distance_ = 30;
    bool attached_ = true;
};
}
