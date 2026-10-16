#pragma once

#include "graphics_device.hpp"

namespace pyroclast
{
// Returns nullptr for a valid frame, otherwise the first violation.
const char* validate_frame(const Frame& frame);

bool owned_by(const Frame& frame, const GraphicsDevice& device);

const char* submission_error(const Frame& frame, const GraphicsDevice& device, bool capture_enabled, bool overlay_enabled);

// Commands a frame records; the readback copy, clears, a capture's copy and the overlay are not counted.
struct FrameWork
{
    std::uint32_t dispatches = 0;
    std::uint32_t passes = 0;
    std::uint32_t draws = 0;
};
FrameWork count_work(const Frame& frame);
}
