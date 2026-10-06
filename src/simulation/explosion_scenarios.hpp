#pragma once

#include <cstdint>

namespace pyroclast
{
constexpr std::uint32_t maximum_explosions = 100;

constexpr bool is_explosion_scenario(std::uint32_t explosions)
{
    return explosions == 1 || explosions == 10 || explosions == 50 || explosions == maximum_explosions;
}
}
