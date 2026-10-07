#include "environment.hpp"

#include <cstring>

namespace pyroclast
{
Environment::Environment()
{
    using namespace terrain_layout;
    bytes_.resize(environment_bytes);
    const auto heights = std::as_bytes(terrain_.heights());
    std::memcpy(bytes_.data(), heights.data(), heights.size());
    for (std::uint32_t cell = 0; cell < environment_grid_cells * environment_grid_cells; ++cell)
    {
        const GpuGridCell empty{0, 0, empty_ceiling, 0};
        std::memcpy(bytes_.data() + environment_grid_offset + cell * environment_cell_bytes, &empty, sizeof(empty));
    }
}
}
