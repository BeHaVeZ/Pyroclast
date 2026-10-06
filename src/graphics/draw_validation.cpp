#include "draw_validation.hpp"

#include <algorithm>

namespace pyroclast
{
const char* validate_draw(const DrawCommand& draw, bool half_resolution_smoke)
{
    const auto records = draw.vertices.record_count();
    if (draw.ranges.empty() || draw.vertices.stride() != particle_layout::particle_stride)
    {
        return "Draw requires a particle buffer and at least one range.";
    }
    for (const auto& range : draw.ranges)
    {
        if (range.record_count == 0 || range.first_record > records ||
            range.record_count > records - range.first_record)
        {
            return "Draw range lies outside the vertex buffer.";
        }
        if (range.layer != particle_layout::particle_layer_emissive && range.layer != particle_layout::particle_layer_smoke)
        {
            return "Draw range names an unknown particle layer.";
        }
    }
    const auto is_smoke = [](const DrawRange& range) {
        return range.pipeline.blend() == BlendMode::premultiplied_alpha;
    };
    if (half_resolution_smoke && (!is_smoke(draw.ranges.front()) ||
        std::count_if(draw.ranges.begin(), draw.ranges.end(), is_smoke) != 1))
    {
        return "Half-resolution rendering requires one leading particle smoke range.";
    }
    if (draw.compute && (&draw.compute->pipeline.buffer() != &draw.vertices ||
        draw.compute->group_count == 0 || draw.compute->group_count > 65535 ||
        draw.compute->constants[0] != records))
    {
        return "Compute dispatch must cover the drawn buffer with a valid group count.";
    }
    return nullptr;
}
}
