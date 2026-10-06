#pragma once

#include "../simulation/particle_system.hpp"

namespace pyroclast
{
class ExplosionRenderer final
{
public:
    bool initialize(GraphicsDevice& device, bool half_resolution_smoke);
    RenderResult render(GraphicsDevice& device, ParticleSystem& particles, float aspect_ratio, double elapsed_seconds);

private:
    std::unique_ptr<RenderTarget> smoke_target_;
};
}
