#pragma once

#include "../graphics/particle_layout.hpp"

#include <cstdint>

namespace pyroclast
{
constexpr std::uint32_t maximum_explosions = particle_layout::maximum_explosions;

constexpr bool is_explosion_scenario(std::uint32_t explosions)
{
    return explosions == 1 || explosions == 10 || explosions == 50 || explosions == maximum_explosions;
}
}
