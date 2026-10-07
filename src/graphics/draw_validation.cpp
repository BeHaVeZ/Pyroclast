#include "draw_validation.hpp"

namespace pyroclast
{
const char* validate_frame(const Frame& frame)
{
    const auto records = frame.vertices.size() / particle_layout::particle_stride;
    if (frame.passes.empty() || frame.vertices.size() % particle_layout::particle_stride != 0)
    {
        return "Frame requires a particle buffer and at least one pass.";
    }
    for (std::size_t index = 0; index < frame.passes.size(); ++index)
    {
        const auto& pass = frame.passes[index];
        const bool last = index + 1 == frame.passes.size();
        if (last != (pass.target == nullptr))
        {
            return "The last pass, and only the last, must draw to the backbuffer.";
        }
        bool background_drawn = pass.background == nullptr;
        for (std::size_t earlier = 0; earlier < index; ++earlier)
        {
            if (frame.passes[earlier].target == pass.target)
            {
                return "A render target is drawn by more than one pass.";
            }
            background_drawn = background_drawn || frame.passes[earlier].target == pass.background;
        }
        if (!background_drawn)
        {
            return "A pass background must be a target drawn earlier in the frame.";
        }
        for (const auto& range : pass.draws)
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
    }
    if (frame.compute && (&frame.compute->pipeline.buffer() != &frame.vertices ||
        frame.compute->group_count == 0 || frame.compute->group_count > 65535 ||
        frame.compute->constants[0] != records))
    {
        return "Compute dispatch must cover the drawn buffer with a valid group count.";
    }
    return nullptr;
}
}
