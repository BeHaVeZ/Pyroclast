#include "simulation/environment.hpp"

#include <cstdio>
#include <cstring>

namespace
{
int failures = 0;

void check(bool value, const char* name)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}
}

int main()
{
    using namespace pyroclast;
    namespace layout = terrain_layout;
    const Environment environment;
    const auto bytes = environment.bytes();

    static_assert(layout::environment_grid_offset == 264208 && layout::environment_boxes_offset == 280592 &&
        layout::environment_flags_offset == 282640 && layout::environment_bytes == 282656);
    static_assert(layout::environment_grid_offset % 16 == 0 && layout::environment_boxes_offset % 16 == 0 &&
        layout::environment_flags_offset % 16 == 0 && layout::environment_bytes % 4 == 0);
    static_assert(layout::environment_grid_offset >= Terrain::resolution * Terrain::resolution * sizeof(float));
    check(bytes.size() == layout::environment_bytes, "the buffer is environment_bytes long");
    const auto heights = std::as_bytes(environment.terrain().heights());
    check(std::memcmp(bytes.data(), heights.data(), heights.size()) == 0, "the heights come first, as Terrain lays them out");
    bool padding = true;
    for (std::size_t index = heights.size(); index < layout::environment_grid_offset; ++index) padding &= bytes[index] == std::byte{0};
    check(padding, "the padding before the grid is zero");

    if (failures == 0) std::puts("environment tests passed");
    return failures == 0 ? 0 : 1;
}
