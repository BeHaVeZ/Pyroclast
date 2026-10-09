#include "counter_log.hpp"

#include <algorithm>
#include <cstdio>

namespace pyroclast
{
std::uint64_t counter_checksum(std::uint64_t checksum, const ParticleCounters& counters)
{
    const std::uint32_t words[] = {counters.tick, counters.alive, counters.smoke, counters.light};
    for (const auto word : words)
    {
        for (int byte = 0; byte < 4; ++byte) checksum = (checksum ^ ((word >> (8 * byte)) & 0xff)) * 0x100000001b3ull;
    }
    return checksum;
}

void CounterLog::add_frame(const Frame& frame)
{
    rows_.push_back({frame, std::nullopt, std::nullopt, 0});
}

void CounterLog::set_cpu_ms(double milliseconds)
{
    if (!rows_.empty()) rows_.back().cpu_ms = milliseconds;
}

void CounterLog::add_timing(const GpuTimings& timings)
{
    if (next_timing_ >= rows_.size())
    {
        ++mismatches_;
        return;
    }
    rows_[next_timing_++].gpu = timings;
}

std::vector<std::string_view> CounterLog::region_labels() const
{
    std::vector<std::string_view> labels;
    for (const auto& row : rows_)
    {
        if (!row.gpu) continue;
        for (std::uint32_t index = 0; index < row.gpu->region_count; ++index)
        {
            const std::string_view label = row.gpu->regions[index].label ? row.gpu->regions[index].label : "";
            if (std::find(labels.begin(), labels.end(), label) == labels.end()) labels.push_back(label);
        }
    }
    return labels;
}

CounterLog::Summary CounterLog::summary() const
{
    Summary summary;
    std::vector<double> cpu, frame, submit, wait, present, draw, compute, gpu_frame;
    const auto labels = region_labels();
    std::vector<std::vector<double>> regions(labels.size());
    std::uint64_t alive_sum = 0;
    std::uint64_t visible_sum = 0;
    std::uint64_t offscreen_sum = 0;
    std::size_t measured_records = 0;
    bool first = true;
    for (std::size_t index = 0; index < rows_.size(); ++index)
    {
        const auto& row = rows_[index];
        if (row.frame.phase != FramePhase::measured) continue;
        ++summary.frames;
        cpu.push_back(row.cpu_ms);
        if (index + 1 < rows_.size()) frame.push_back(rows_[index + 1].frame.begin_ms - row.frame.begin_ms);
        submit.push_back(row.frame.submit_ms);
        wait.push_back(row.frame.device.wait_ms);
        present.push_back(row.frame.device.present_ms);
        summary.peak_active = std::max(summary.peak_active, row.frame.active);
        const auto& work = row.frame.work;
        summary.work_min = first ? work : FrameWork{std::min(summary.work_min.dispatches, work.dispatches),
            std::min(summary.work_min.passes, work.passes), std::min(summary.work_min.draws, work.draws)};
        summary.work_max = first ? work : FrameWork{std::max(summary.work_max.dispatches, work.dispatches),
            std::max(summary.work_max.passes, work.passes), std::max(summary.work_max.draws, work.draws)};
        first = false;
        if (row.gpu)
        {
            ++summary.gpu_samples;
            draw.push_back(row.gpu->draw_ms);
            compute.push_back(row.gpu->compute_ms);
            gpu_frame.push_back(row.gpu->frame_ms);
            for (std::uint32_t region = 0; region < row.gpu->region_count; ++region)
            {
                const std::string_view label = row.gpu->regions[region].label ? row.gpu->regions[region].label : "";
                const auto found = std::find(labels.begin(), labels.end(), label) - labels.begin();
                regions[static_cast<std::size_t>(found)].push_back(row.gpu->regions[region].milliseconds);
            }
        }
        if (row.counters)
        {
            const auto& counters = *row.counters;
            ++summary.counter_records;
            summary.peak_alive = std::max(summary.peak_alive, counters.alive);
            alive_sum += counters.alive;
            summary.checksum = counter_checksum(summary.checksum, counters);
            if (counters.measured)
            {
                const auto listed = counters.smoke + counters.light;
                const auto visible = listed - std::min(listed, counters.offscreen);
                ++measured_records;
                visible_sum += visible;
                offscreen_sum += counters.offscreen;
                if (!summary.visible) summary.visible = CountSummary{};
                if (!summary.offscreen) summary.offscreen = CountSummary{};
                summary.visible->peak = std::max(summary.visible->peak, visible);
                summary.offscreen->peak = std::max(summary.offscreen->peak, counters.offscreen);
            }
        }
    }
    const auto statistics = [](std::vector<double>& samples) {
        return samples.empty() ? std::optional<FrameStatistics>() : std::optional(summarize(samples));
    };
    summary.cpu = statistics(cpu);
    summary.frame = statistics(frame);
    summary.submit = statistics(submit);
    summary.wait = statistics(wait);
    summary.present = statistics(present);
    summary.draw = statistics(draw);
    summary.compute = statistics(compute);
    summary.gpu_frame = statistics(gpu_frame);
    for (std::size_t index = 0; index < labels.size(); ++index)
    {
        if (regions[index].empty()) continue;
        const auto samples = regions[index].size();
        summary.regions.push_back({std::string(labels[index]), samples, summarize(regions[index])});
    }
    if (summary.counter_records != 0) summary.mean_alive = static_cast<double>(alive_sum) / static_cast<double>(summary.counter_records);
    if (measured_records != 0)
    {
        summary.visible->mean = static_cast<double>(visible_sum) / static_cast<double>(measured_records);
        summary.offscreen->mean = static_cast<double>(offscreen_sum) / static_cast<double>(measured_records);
    }
    return summary;
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
    if (evicted_) text += ",evicted";
    if (realism_) text += ",residual,ghosts,clusters,forced,linger_scale,deferred";
    const auto labels = benchmark_ ? region_labels() : std::vector<std::string_view>();
    if (benchmark_)
    {
        text += ",phase,frame_ms,submit_ms,wait_ms,present_ms,gpu_frame_ms,dispatches,passes,draws";
        for (const auto label : labels) text += ",region:" + std::string(label);
    }
    text += '\n';
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
            std::snprintf(line, sizeof(line), "%.6f,%.6f,", row.gpu->draw_ms, row.gpu->compute_ms);
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
        if (realism_)
        {
            if (row.counters) std::snprintf(line, sizeof(line), ",%u,%u", row.counters->residual, row.counters->ghosts);
            else std::snprintf(line, sizeof(line), ",,");
            text += line;
            std::snprintf(line, sizeof(line), ",%u,%llu,%u,%llu", frame.clusters, static_cast<unsigned long long>(frame.forced), frame.linger_scale,
                static_cast<unsigned long long>(frame.deferred));
            text += line;
        }
        if (benchmark_)
        {
            text += frame.phase == FramePhase::warmup ? ",warmup," : ",measured,";
            if (index + 1 < rows_.size())
            {
                std::snprintf(line, sizeof(line), "%.6f", rows_[index + 1].frame.begin_ms - frame.begin_ms);
                text += line;
            }
            std::snprintf(line, sizeof(line), ",%.6f,%.6f,%.6f,", frame.submit_ms, frame.device.wait_ms, frame.device.present_ms);
            text += line;
            if (row.gpu)
            {
                std::snprintf(line, sizeof(line), "%.6f", row.gpu->frame_ms);
                text += line;
            }
            std::snprintf(line, sizeof(line), ",%u,%u,%u", frame.work.dispatches, frame.work.passes, frame.work.draws);
            text += line;
            for (const auto label : labels)
            {
                text += ',';
                if (!row.gpu) continue;
                for (std::uint32_t region = 0; region < row.gpu->region_count; ++region)
                {
                    const std::string_view found = row.gpu->regions[region].label ? row.gpu->regions[region].label : "";
                    if (found != label) continue;
                    std::snprintf(line, sizeof(line), "%.6f", row.gpu->regions[region].milliseconds);
                    text += line;
                    break;
                }
            }
        }
        text += '\n';
    }
    return text;
}
}
