#include "draw_validation.hpp"

#include <algorithm>
#include <cstdint>

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
    if (const auto& readback = frame.readback; readback.size != 0 || readback.source)
    {
        if (!readback.source || readback.size == 0 || readback.size > max_readback_bytes || readback.size % 4 != 0 ||
            readback.offset % 4 != 0 || std::uint64_t{readback.offset} + readback.size > readback.source->size())
        {
            return "A readback needs a source and 4-byte aligned bytes within it, at most max_readback_bytes.";
        }
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
        if (pass.target && pass.target->description().format == TargetFormat::depth)
        {
            return "A pass's colour target must have a colour format.";
        }
        const auto earlier = frame.passes.first(index);
        const auto drawn_earlier = [&earlier](const RenderTarget* target) {
            return std::any_of(earlier.begin(), earlier.end(), [target](const Pass& other) { return other.target == target; });
        };
        const auto depth_written_earlier = [&earlier](const RenderTarget* target) {
            return std::any_of(earlier.begin(), earlier.end(), [target](const Pass& other) { return other.depth == target && other.depth_write; });
        };
        if (pass.load && (!pass.target || !drawn_earlier(pass.target)))
        {
            return "A pass can load only a target drawn earlier in the frame.";
        }
        if (!pass.load && pass.target && drawn_earlier(pass.target))
        {
            return "A render target is drawn by more than one pass without loading it.";
        }
        if (pass.depth)
        {
            if (pass.depth->description().format != TargetFormat::depth)
            {
                return "A pass's depth target must have the depth format.";
            }
            if (pass.depth->description().divisor != (pass.target ? pass.target->description().divisor : 1))
            {
                return "A depth target must be the size of its pass's colour target.";
            }
            if (pass.depth_write == depth_written_earlier(pass.depth))
            {
                return pass.depth_write ? "A depth target is cleared and written by more than one pass." :
                    "A read-only depth target must be written by an earlier pass of the frame.";
            }
        }
        else if (pass.depth_write)
        {
            return "A writable depth pass needs a depth target.";
        }
        for (const auto* input : pass.inputs)
        {
            if (!input) continue;
            if (!drawn_earlier(input) && !depth_written_earlier(input))
            {
                return "A pass input must be a target drawn earlier in the frame.";
            }
            if (input == pass.target || (input == pass.depth && pass.depth_write))
            {
                return "A pass cannot sample a target it writes.";
            }
        }
        for (const auto& draw : pass.draws)
        {
            if (draw.indirect)
            {
                if (draw.indirect_offset % 4 != 0 || std::uint64_t{draw.indirect_offset} + 16 > draw.indirect->size())
                {
                    return "Indirect draw arguments must be four aligned words within their buffer.";
                }
            }
            else if (draw.vertex_count == 0 || draw.instance_count == 0)
            {
                return "A draw needs at least one vertex and one instance.";
            }
            if (std::any_of(draw.bindings.write.begin(), draw.bindings.write.end(), [](const Buffer* buffer) { return buffer != nullptr; }))
            {
                return "Draws cannot bind writable buffers.";
            }
            if (draw.pipeline.target() != (pass.target ? pass.target->description().format : TargetFormat::color))
            {
                return "A pipeline's colour format must match its pass's target.";
            }
            if ((draw.pipeline.depth() == DepthMode::none) != (pass.depth == nullptr))
            {
                return "A pipeline's depth mode must match whether its pass has a depth target.";
            }
            if (draw.pipeline.depth() == DepthMode::test_write && !pass.depth_write)
            {
                return "A depth-writing pipeline needs a writable depth target.";
            }
        }
    }
    return nullptr;
}

bool owned_by(const Frame& frame, const GraphicsDevice& device)
{
    const auto owned = [&device](const auto* resource) { return resource == nullptr || resource->owner() == &device; };
    const auto bindings_owned = [&owned](const Bindings& bindings) {
        return std::all_of(bindings.read.begin(), bindings.read.end(), owned) && std::all_of(bindings.write.begin(), bindings.write.end(), owned);
    };
    if (!owned(frame.readback.source)) return false;
    for (const auto& dispatch : frame.dispatches)
    {
        if (!owned(&dispatch.pipeline) || !bindings_owned(dispatch.bindings)) return false;
    }
    for (const auto& pass : frame.passes)
    {
        if (!owned(pass.target) || !owned(pass.depth) || !std::all_of(pass.inputs.begin(), pass.inputs.end(), owned)) return false;
        for (const auto& draw : pass.draws)
        {
            if (!owned(&draw.pipeline) || !bindings_owned(draw.bindings) || !owned(draw.indirect)) return false;
        }
    }
    return true;
}

const char* submission_error(const Frame& frame, const GraphicsDevice& device, bool capture_enabled, bool overlay_enabled)
{
    if (!owned_by(frame, device)) return "Frame uses resources from another graphics device.";
    if (frame.capture && !capture_enabled) return "Frame capture requires a device created with capture enabled.";
    if (frame.overlay && !overlay_enabled) return "Frame overlay requires a device created with the debug overlay.";
    return validate_frame(frame);
}

FrameWork count_work(const Frame& frame)
{
    FrameWork work{static_cast<std::uint32_t>(frame.dispatches.size()), static_cast<std::uint32_t>(frame.passes.size()), 0};
    for (const auto& pass : frame.passes) work.draws += static_cast<std::uint32_t>(pass.draws.size());
    return work;
}
}
