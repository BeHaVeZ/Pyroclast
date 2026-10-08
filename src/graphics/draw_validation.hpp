#pragma once

#include "graphics_device.hpp"

namespace pyroclast
{
const char* validate_frame(const Frame& frame);

struct FrameWork
{
    std::uint32_t dispatches = 0;
    std::uint32_t passes = 0;
    std::uint32_t draws = 0;
};
FrameWork count_work(const Frame& frame);
}
