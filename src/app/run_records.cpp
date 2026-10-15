#include "run_records.hpp"
#include "../simulation/simulation_clock.hpp"
#include "frame_baseline.hpp"

#include <cmath>
#include <cstdio>

namespace pyroclast
{
std::string statistics_json(const std::optional<FrameStatistics>& statistics)
{
    if (!statistics) return "null";
    char text[224];
    std::snprintf(text, sizeof(text), "{\"average\":%.6f,\"median\":%.6f,\"p95\":%.6f,\"p99\":%.6f,\"worst_1_percent\":%.6f,\"max\":%.6f}",
        statistics->average, statistics->median, statistics->p95, statistics->p99, statistics->worst_1_percent, statistics->max);
    return text;
}

const char* pose_name(const ApplicationOptions& options)
{
    return options.camera ? "custom" : view_name(options.view);
}

std::string view_keys(float vertical_fov, bool borderless)
{
    std::string keys;
    if (vertical_fov != Camera{}.vertical_fov)
    {
        char text[48];
        std::snprintf(text, sizeof(text), ",\"fov\":%.3f", static_cast<double>(vertical_fov) * 57.29577951308232);
        keys += text;
    }
    if (borderless) keys += ",\"borderless\":true";
    return keys;
}

int ScenarioRun::report(const Sandbox& sandbox, const ApplicationOptions& options, std::string_view gpu_name, int width, int height,
    bool overlay, const std::string& extra, const CounterLog::Summary* summary, const std::string& tail)
{
    const auto statistics = [](std::vector<double>& samples) {
        if (samples.empty()) return std::string("null");
        const auto summary = summarize(samples);
        char text[160];
        std::snprintf(text, sizeof(text), "{\"average\":%.6f,\"median\":%.6f,\"p95\":%.6f,\"p99\":%.6f,\"max\":%.6f}",
            summary.average, summary.median, summary.p95, summary.p99, summary.max);
        return std::string(text);
    };
    const auto frames = summary ? summary->frames : cpu_ms_.size();
    const auto peak_effects = summary ? summary->peak_active : peak_effects_;
    const auto counter_samples = summary ? summary->counter_records : counter_samples_;
    const auto peak_alive = summary ? summary->peak_alive : peak_alive_;
    const auto checksum = summary ? summary->checksum : checksum_;
    SDL_Log("Scenario %.*s finished: %zu frames, %llu effects started, %llu dropped, peak %u active.",
        static_cast<int>(scenario_.name.size()), scenario_.name.data(), frames,
        static_cast<unsigned long long>(sandbox.effects.started()), static_cast<unsigned long long>(sandbox.effects.dropped()),
        peak_effects);
    const double mean_alive = summary ? summary->mean_alive :
        counter_samples_ != 0 ? static_cast<double>(alive_sum_) / static_cast<double>(counter_samples_) : 0.0;
    SDL_Log("Live particles: %zu counter samples, peak %u alive, mean %.1f, checksum %016llx.", counter_samples, peak_alive,
        mean_alive, static_cast<unsigned long long>(checksum));
    std::string gpu = "\"";
    for (const char character : gpu_name) if (character != '"' && character != '\\' && static_cast<unsigned char>(character) >= 0x20) gpu += character;
    gpu += '"';
    const auto cpu = summary ? statistics_json(summary->cpu) : statistics(cpu_ms_);
    const auto draw = summary ? statistics_json(summary->draw) : statistics(draw_ms_);
    const auto compute = summary ? statistics_json(summary->compute) : statistics(compute_ms_);
    const auto gpu_samples = summary ? summary->gpu_samples : draw_ms_.size();
    std::string regions;
    if (summary)
    {
        for (const auto& region : summary->regions)
        {
            const auto& values = region.statistics;
            char entry[320];
            std::snprintf(entry, sizeof(entry),
                "%s{\"label\":\"%s\",\"ms\":%.6f,\"samples\":%zu,\"median\":%.6f,\"p95\":%.6f,\"p99\":%.6f,\"worst_1_percent\":%.6f,\"max\":%.6f}",
                regions.empty() ? "" : ",", region.label.c_str(), values.average, region.samples, values.median, values.p95,
                values.p99, values.worst_1_percent, values.max);
            regions += entry;
        }
    }
    for (const auto& sum : summary ? std::vector<RegionSum>() : regions_)
    {
        char entry[160];
        std::snprintf(entry, sizeof(entry), "%s{\"label\":\"%s\",\"ms\":%.6f,\"samples\":%zu}", regions.empty() ? "" : ",",
            sum.label, sum.milliseconds / static_cast<double>(sum.samples), sum.samples);
        regions += entry;
    }
    const std::string benchmark_key = benchmark_.empty() ? std::string() : "\"benchmark\":\"" + benchmark_ + "\",";
    std::string camera = options.camera ? ",\"camera\":" + camera_json(*options.camera) : std::string();
    camera += view_keys(vertical_fov_, options.borderless);
    if (!extra.empty()) camera += "," + extra;
    std::printf("{%s\"scenario\":\"%.*s\",\"valid\":%s,\"build\":\"%s\",\"renderer\":\"%s\",\"gpu\":%s,\"width\":%d,\"height\":%d,"
        "\"smoke_resolution\":\"%s\",\"scene_format\":\"%s\",\"view\":\"%s\"%s,\"overlay\":%s,\"effect_lines\":%s,\"gpu_regions\":%s,"
        "\"preset\":\"%.*s\",\"layer_mask\":%u,\"wind\":[%.2f,%.1f,%.2f],\"ticks\":%u,\"frames\":%zu,\"effects_started\":%llu,\"effects_dropped\":%llu,\"effects_evicted\":%llu,\"peak_effects\":%u,"
        "\"particles\":{\"samples\":%zu,\"peak_alive\":%u,\"mean_alive\":%.3f,\"checksum\":\"%016llx\"},\"cpu_ms\":%s,"
        "\"gpu_ms\":{\"samples\":%zu,\"draw\":%s,\"compute\":%s,\"regions\":[%s]}%s}\n",
        benchmark_key.c_str(), static_cast<int>(scenario_.name.size()), scenario_.name.data(), valid_ ? "true" : "false",
        build_configuration,
        renderer_name(options.renderer), gpu.c_str(), width, height, smoke_resolution_name(options.scalability.smoke_resolution),
        scene_format_name(options.scene_format), pose_name(options), camera.c_str(), overlay ? "true" : "false",
        sandbox.views.effect_bounds ? "true" : "false", options.gpu_regions ? "true" : "false",
        static_cast<int>(preset_name(options.preset).size()), preset_name(options.preset).data(),
        sandbox.layer_mask, static_cast<double>(sandbox.wind.speed), static_cast<double>(sandbox.wind.direction_degrees),
        static_cast<double>(sandbox.wind.gustiness), scenario_.duration_ticks, frames,
        static_cast<unsigned long long>(sandbox.effects.started()), static_cast<unsigned long long>(sandbox.effects.dropped()),
        static_cast<unsigned long long>(sandbox.effects.evicted()), peak_effects, counter_samples, peak_alive, mean_alive,
        static_cast<unsigned long long>(checksum), cpu.c_str(), gpu_samples, draw.c_str(), compute.c_str(), regions.c_str(), tail.c_str());
    std::fflush(stdout);
    return valid_ ? 0 : 1;
}

std::string benchmark_tail(const ScenarioRun& run, const CounterLog::Summary& summary, const GraphicsDevice& device,
    Renderer renderer, bool gpu_validation, std::optional<float> display_hz, bool focused,
    const std::optional<GpuMemory>& memory_start, const std::optional<GpuMemory>& memory_end)
{
    const auto memory_json = [](const GpuMemory& memory) {
        char text[192];
        std::snprintf(text, sizeof(text), "{\"local_usage\":%llu,\"local_budget\":%llu,\"non_local_usage\":%llu,\"non_local_budget\":%llu}",
            static_cast<unsigned long long>(memory.local_usage), static_cast<unsigned long long>(memory.local_budget),
            static_cast<unsigned long long>(memory.non_local_usage), static_cast<unsigned long long>(memory.non_local_budget));
        return std::string(text);
    };
    const auto count_json = [](const std::optional<CounterLog::CountSummary>& count) {
        if (!count) return std::string("null");
        char text[96];
        std::snprintf(text, sizeof(text), "{\"mean\":%.3f,\"peak\":%u}", count->mean, count->peak);
        return std::string(text);
    };
    const auto present = device.present_info();
    char text[512];
    std::string tail = ",\"schema\":1,\"invalid_reason\":";
    tail += run.invalid_reason() ? "\"" + std::string(run.invalid_reason()) + "\"" : std::string("null");
    std::snprintf(text, sizeof(text),
        ",\"warmup_frames\":%u,\"skipped_frames\":%zu,\"focused\":%s,\"vsync\":false,\"gpu_validation\":%s,\"capacity\":%u,\"present_mode\":\"%.*s\",\"swapchain_images\":%u,",
        benchmark_warmup_frames, run.skipped_frames(), focused ? "true" : "false", gpu_validation ? "true" : "false",
        ParticleSystem::capacity,
        static_cast<int>(present.mode.size()), present.mode.data(), present.images);
    tail += text;
    if (display_hz) std::snprintf(text, sizeof(text), "\"display_hz\":%.3f", static_cast<double>(*display_hz));
    else std::snprintf(text, sizeof(text), "\"display_hz\":null");
    tail += text;
    tail += ",\"frame_ms\":" + statistics_json(summary.frame) + ",\"submit_ms\":" + statistics_json(summary.submit) +
        ",\"wait_ms\":" + statistics_json(summary.wait) + ",\"present_ms\":" + statistics_json(summary.present) +
        ",\"gpu_frame_ms\":" + statistics_json(summary.gpu_frame);
    std::snprintf(text, sizeof(text), ",\"work\":{\"dispatches\":[%u,%u],\"passes\":[%u,%u],\"draws\":[%u,%u]}",
        summary.work_min.dispatches, summary.work_max.dispatches, summary.work_min.passes, summary.work_max.passes,
        summary.work_min.draws, summary.work_max.draws);
    tail += text;
    tail += ",\"counts\":{\"visible\":" + count_json(summary.visible) + ",\"offscreen\":" + count_json(summary.offscreen) + ",\"culled\":null}";
    tail += ",\"gpu_memory\":";
    if (memory_start && memory_end)
    {
        tail += std::string("{\"source\":\"") + (renderer == Renderer::d3d12 ? "dxgi" : "vk_ext_memory_budget") +
            "\",\"start\":" + memory_json(*memory_start) + ",\"end\":" + memory_json(*memory_end) + "}";
    }
    else tail += "null";
    return tail;
}


void RunRecords::after_frame(const Sandbox& sandbox, RenderResult result)
{
    if (result == RenderResult::submitted && realism_)
    {
        const auto& rotor = sandbox.last_step.rotor;
        if (rotor.exists && rotor.listed) lowest_hub_cm_ = std::min(lowest_hub_cm_, rotor.hub_height_cm);
        peak_dust_ = std::max(peak_dust_, rotor.census.alive);
        const auto& jet = sandbox.last_step.jet;
        peak_knots_ = std::max(peak_knots_, jet.wake.count);
        peak_jet_dust_ = std::max(peak_jet_dust_, jet.census.dust);
        if (jet.slot)
        {
            const auto& slot = sandbox.effects.slots()[*jet.slot];
            jet_slot_ = {slot.start_tick, slot.start_tick + slot.lifetime_ticks};
        }
        if (rotor.slot)
        {
            const auto& slot = sandbox.effects.slots()[*rotor.slot];
            rotor_slot_ = {slot.start_tick, slot.start_tick + slot.lifetime_ticks};
        }
        if ((rotor.slot || jet.slot) && sandbox.effects.explosions() == 0 && sandbox.renderer.particles_drawn())
        {
            const std::uint32_t rotor_records = rotor.slot ? rotor.census.alive + (rotor.listed ? rotor_layout::rotor_disc_records : 0u) : 0u;
            const std::uint32_t jet_records = jet.slot ? jet.census.dust : 0u;
            rotor_expected_.push_back({sandbox.clock.tick(), rotor_records + jet_records});
        }
    }
    peak_clusters_ = std::max(peak_clusters_, sandbox.renderer.listed_clusters());
}

void RunRecords::add_counters(const ParticleCounters& counters)
{
    peak_residual_ = std::max(peak_residual_, counters.residual);
    ghosts_ = std::max(ghosts_, counters.ghosts);
    while (!rotor_expected_.empty() && rotor_expected_.front().first < counters.tick) rotor_expected_.pop_front();
    if (rotor_expected_.empty() || rotor_expected_.front().first != counters.tick) return;
    const auto expected = rotor_expected_.front().second;
    rotor_expected_.pop_front();
    if (counters.smoke - counters.residual == expected) return;
    if (twin_mismatches_ == 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Rotor slot at tick %u: the GPU counts %u records, the CPU's twin %u.", counters.tick,
            counters.smoke - counters.residual, expected);
    }
    ++twin_mismatches_;
}

std::string RunRecords::json(const Sandbox& sandbox) const
{
    std::string text = coverage.json();
    if (automatic_)
    {
        const auto& policy = sandbox.renderer.smoke_policy();
        text += (text.empty() ? "" : ",") + std::string("\"half_frames\":") + std::to_string(policy.half_frames()) +
            ",\"switches\":" + std::to_string(policy.switches());
    }
    const auto& settings = sandbox.scalability;
    std::string options;
    char number[64];
    const auto add = [&options](const std::string& entry) { options += (options.empty() ? "" : ",") + entry; };
    if (settings.smoke_budget.count || settings.smoke_budget.limit > 0)
    {
        std::snprintf(number, sizeof(number), "%s:%g", settings.smoke_budget.count ? "count" : "coverage",
            SmokeResolutionPolicy::limit(settings.smoke_budget));
        add(std::string("\"smoke_budget\":\"") + number + "\"");
    }
    if (settings.half_light) add("\"half_light\":true");
    if (settings.smoke_lod > 0)
    {
        std::snprintf(number, sizeof(number), "%g", static_cast<double>(settings.smoke_lod));
        add(std::string("\"smoke_lod\":") + number);
    }
    if (settings.faint_floor > 0)
    {
        std::snprintf(number, sizeof(number), "%g", static_cast<double>(settings.faint_floor));
        add(std::string("\"faint_floor\":") + number);
    }
    if (settings.effect_order != EffectOrder::origin)
    {
        add(std::string("\"effect_order\":\"") + effect_order_name(settings.effect_order) + "\"");
    }
    if (settings.slot_policy != SlotPolicy::drop_new)
    {
        add(std::string("\"slot_policy\":\"") + slot_policy_name(settings.slot_policy) + "\"");
    }
    if (!options.empty()) text += (text.empty() ? "" : ",") + std::string("\"scalability\":{") + options + "}";
    if (!realism_) return text;
    char lingering[256];
    std::snprintf(lingering, sizeof(lingering),
        "\"realism\":{\"linger_seconds\":%u,\"humidity_percent\":%u,\"forced_fades\":%llu,\"deferred\":%llu,\"peak_clusters\":%u,"
        "\"peak_residual\":%u,\"ghosts\":%u}",
        sandbox.linger.linger_ticks / ticks_per_second, static_cast<unsigned>(std::lround(sandbox.humidity * 100)),
        static_cast<unsigned long long>(sandbox.effects.residual().forced_fades()),
        static_cast<unsigned long long>(sandbox.effects.deferred()), peak_clusters_, peak_residual_, ghosts_);
    text += (text.empty() ? "" : ",") + std::string(lingering);
    if (const auto& helicopter = sandbox.rotorcraft.helicopter())
    {
        char flight[320];
        std::snprintf(flight, sizeof(flight),
            ",\"helicopter\":{\"flight\":\"%s\",\"listed\":[%u,%u],\"rotor_slot\":[%u,%u],\"peak_dust\":%u,\"lowest_hub_cm\":%d,\"mismatches\":%u}",
            helicopter->plan.name.c_str(), helicopter->plan.list_tick, helicopter->plan.unlist_tick, rotor_slot_.first, rotor_slot_.second,
            peak_dust_, lowest_hub_cm_ == std::numeric_limits<std::int32_t>::max() ? -1 : lowest_hub_cm_, twin_mismatches_);
        text += flight;
    }
    if (const auto& jet = sandbox.jetcraft.flight())
    {
        char passes[256];
        std::snprintf(passes, sizeof(passes),
            ",\"jet\":{\"flight\":\"%s\",\"passes\":%zu,\"peak_knots\":%u,\"jet_slot\":[%u,%u],\"peak_dust\":%u,\"mismatches\":%u}",
            jet->name.c_str(), jet->passes.size(), peak_knots_, jet_slot_.first, jet_slot_.second, peak_jet_dust_, twin_mismatches_);
        text += passes;
    }
    return text;
}
}
