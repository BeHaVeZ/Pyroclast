#pragma once

#include "../simulation/particle_system.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pyroclast
{
class CounterLog final
{
public:
    explicit CounterLog(bool coverage = false) : coverage_(coverage) {}
    struct Frame
    {
        std::uint32_t tick = 0;
        std::uint32_t steps = 0;
        std::uint32_t active = 0;
        std::uint64_t started = 0;
        std::uint64_t dropped = 0;
        bool readback = false;
    };

    void add_frame(const Frame& frame);
    void set_cpu_ms(double milliseconds);
    void add_timing(double draw_ms, double compute_ms);
    void add_counters(const ParticleCounters& counters);
    std::size_t mismatches() const { return mismatches_; }
    std::size_t size() const { return rows_.size(); }
    std::string csv() const;

private:
    struct Row
    {
        Frame frame;
        std::optional<ParticleCounters> counters;
        std::optional<std::array<double, 2>> gpu;
        double cpu_ms = 0;
    };
    std::vector<Row> rows_;
    std::size_t next_timing_ = 0;
    std::size_t next_counters_ = 0;
    std::size_t mismatches_ = 0;
    bool coverage_ = false;
};
}
