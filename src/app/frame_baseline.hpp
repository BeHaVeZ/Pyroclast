#pragma once

#include <array>
#include <cstddef>

namespace pyroclast
{
class FrameBaseline final
{
public:
    void disable() { enabled_ = false; }
    void record(double milliseconds);

private:
    std::array<double, 600> samples_{};
    std::size_t warmup_ = 120;
    std::size_t count_ = 0;
    bool enabled_ = true;
};
}
