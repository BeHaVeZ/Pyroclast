#include "counter_log.hpp"

#include <cstdio>

namespace pyroclast
{
void CounterLog::add_frame(const Frame& frame)
{
    rows_.push_back({frame, std::nullopt, std::nullopt, 0});
}

void CounterLog::set_cpu_ms(double milliseconds)
{
    if (!rows_.empty()) rows_.back().cpu_ms = milliseconds;
}

void CounterLog::add_timing(double draw_ms, double compute_ms)
{
    if (next_timing_ >= rows_.size())
    {
        ++mismatches_;
        return;
    }
    rows_[next_timing_++].gpu = std::array<double, 2>{draw_ms, compute_ms};
}

void CounterLog::add_counters(const ParticleCounters& counters)
{
    while (next_counters_ < rows_.size() && !rows_[next_counters_].frame.readback) ++next_counters_;
    if (next_counters_ >= rows_.size())
    {
        ++mismatches_;
        return;
    }
    auto& row = rows_[next_counters_++];
    if (counters.tick != row.frame.tick) ++mismatches_;
    row.counters = counters;
}

std::string CounterLog::csv() const
{
    std::string text = "frame,tick,steps,active,started,dropped,counter_tick,alive,smoke,light,debris,draw_ms,compute_ms,cpu_ms";
    if (coverage_) text += ",offscreen,smoke_bin0,smoke_bin1,smoke_bin2,smoke_bin3,coverage_smoke,coverage_light";
    if (lod_) text += ",thinned,faint";
    if (smoke_resolution_) text += ",half";
    text += evicted_ ? ",evicted\n" : "\n";
    char line[256];
    for (std::size_t index = 0; index < rows_.size(); ++index)
    {
        const auto& row = rows_[index];
        const auto& frame = row.frame;
        std::snprintf(line, sizeof(line), "%zu,%u,%u,%u,%llu,%llu,", index, frame.tick, frame.steps, frame.active,
            static_cast<unsigned long long>(frame.started), static_cast<unsigned long long>(frame.dropped));
        text += line;
        if (row.counters)
        {
            const auto& counters = *row.counters;
            std::snprintf(line, sizeof(line), "%u,%u,%u,%u,%u,", counters.tick, counters.alive, counters.smoke, counters.light, counters.debris);
            text += line;
        }
        else text += ",,,,,";
        if (row.gpu)
        {
            std::snprintf(line, sizeof(line), "%.6f,%.6f,", (*row.gpu)[0], (*row.gpu)[1]);
            text += line;
        }
        else text += ",,";
        std::snprintf(line, sizeof(line), "%.4f", row.cpu_ms);
        text += line;
        if (coverage_ && row.counters)
        {
            const auto& counters = *row.counters;
            const auto& bins = counters.smoke_coverage;
            std::snprintf(line, sizeof(line), ",%u,%u,%u,%u,%u,%u,%u", counters.offscreen, bins[0], bins[1], bins[2], bins[3],
                bins[0] + bins[1] + bins[2] + bins[3], counters.light_coverage);
            text += line;
        }
        else if (coverage_) text += ",,,,,,,";
        if (lod_ && row.counters)
        {
            std::snprintf(line, sizeof(line), ",%u,%u", row.counters->thinned, row.counters->faint);
            text += line;
        }
        else if (lod_) text += ",,";
        if (smoke_resolution_) text += frame.half ? ",1" : ",0";
        if (evicted_)
        {
            std::snprintf(line, sizeof(line), ",%llu", static_cast<unsigned long long>(frame.evicted));
            text += line;
        }
        text += '\n';
    }
    return text;
}
}
