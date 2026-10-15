#pragma once

#include "graphics_device.hpp"

namespace pyroclast
{
const char* validate_frame(const Frame& frame);

bool owned_by(const Frame& frame, const GraphicsDevice& device);

const char* submission_error(const Frame& frame, const GraphicsDevice& device, bool capture_enabled, bool overlay_enabled);

struct FrameWork
{
    std::uint32_t dispatches = 0;
    std::uint32_t passes = 0;
    std::uint32_t draws = 0;
};
FrameWork count_work(const Frame& frame);
}
