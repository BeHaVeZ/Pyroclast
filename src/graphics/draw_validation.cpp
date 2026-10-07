#include "draw_validation.hpp"

#include <algorithm>

namespace pyroclast
{
namespace
{
bool reads_and_writes(const Bindings& bindings)
{
    return std::any_of(bindings.write.begin(), bindings.write.end(), [&bindings](const Buffer* written) {
        return written && std::find(bindings.read.begin(), bindings.read.end(), written) != bindings.read.end();
    });
}
}

const char* validate_frame(const Frame& frame)
{
    if (frame.passes.empty())
    {
        return "Frame requires at least one pass.";
    }
    if (frame.constants.size() > binding_layout::frame_constant_bytes)
    {
        return "Frame constants exceed binding_layout::frame_constant_bytes.";
    }
    for (const auto& dispatch : frame.dispatches)
    {
        if (std::any_of(dispatch.groups.begin(), dispatch.groups.end(), [](auto groups) { return groups == 0 || groups > 65535; }))
        {
            return "Dispatch group counts must be between 1 and 65535 in each dimension.";
        }
        if (reads_and_writes(dispatch.bindings))
        {
            return "A dispatch binds the same buffer as readable and writable.";
        }
    }
    for (std::size_t index = 0; index < frame.passes.size(); ++index)
    {
        const auto& pass = frame.passes[index];
        const bool last = index + 1 == frame.passes.size();
        if (last != (pass.target == nullptr))
        {
            return "The last pass, and only the last, must draw to the backbuffer.";
        }
        const auto earlier = frame.passes.first(index);
        if (std::any_of(earlier.begin(), earlier.end(), [&pass](const Pass& other) { return other.target == pass.target; }))
        {
            return "A render target is drawn by more than one pass.";
        }
        for (const auto* input : pass.inputs)
        {
            if (input && std::none_of(earlier.begin(), earlier.end(), [input](const Pass& other) { return other.target == input; }))
            {
                return "A pass input must be a target drawn earlier in the frame.";
            }
        }
        for (const auto& draw : pass.draws)
        {
            if (draw.vertex_count == 0 || draw.instance_count == 0)
            {
                return "A draw needs at least one vertex and one instance.";
            }
            if (std::any_of(draw.bindings.write.begin(), draw.bindings.write.end(), [](const Buffer* buffer) { return buffer != nullptr; }))
            {
                return "Draws cannot bind writable buffers.";
            }
        }
    }
    return nullptr;
}
}
