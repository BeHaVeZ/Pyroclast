#pragma once

#include "../simulation/particle_system.hpp"

namespace pyroclast
{
RenderResult render_explosions(GraphicsDevice& device, ParticleSystem& particles, float aspect_ratio, double elapsed_seconds);
}
