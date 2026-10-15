#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "command_line.hpp"
#include "counter_log.hpp"
#include "explosion_renderer.hpp"
#include "frame_baseline.hpp"
#include "frame_statistics.hpp"
#include "key_bindings.hpp"
#include "sandbox.hpp"
#include "../simulation/explosion_scenarios.hpp"
#include "../tools/debug_ui.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace
{
int save_capture(pyroclast::GraphicsDevice& device, const std::string& path, bool log = true)
{
    auto image = device.read_capture();
    if (!image)
    {
        return 1;
    }
    SDL_Surface* surface = SDL_CreateSurfaceFrom(static_cast<int>(image->width), static_cast<int>(image->height),
        SDL_PIXELFORMAT_RGBA32, image->rgba.data(), static_cast<int>(image->width * 4));
    const bool saved = surface && SDL_SavePNG(surface, path.c_str());
    SDL_DestroySurface(surface);
    if (!saved)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Save capture '%s' failed: %s", path.c_str(), SDL_GetError());
        return 1;
    }
    if (log) SDL_Log("Captured frame: %s (%ux%u).", path.c_str(), image->width, image->height);
    return 0;
}

std::filesystem::path sequence_folder(const std::string& path)
{
    return std::filesystem::path(std::u8string(path.begin(), path.end()));
}

bool check_sequence_folder(const std::string& path)
{
    std::error_code error;
    const auto folder = sequence_folder(path);
    const auto status = std::filesystem::status(folder, error);
    if (status.type() == std::filesystem::file_type::none)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Read capture sequence folder '%s' failed: %s", path.c_str(), error.message().c_str());
        return false;
    }
    if (!std::filesystem::exists(status)) return true;
    if (!std::filesystem::is_directory(status))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Capture sequence path '%s' is not a folder.", path.c_str());
        return false;
    }
    const bool empty = std::filesystem::is_empty(folder, error);
    if (error)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Read capture sequence folder '%s' failed: %s", path.c_str(), error.message().c_str());
        return false;
    }
    if (!empty)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Capture sequence folder '%s' is not empty.", path.c_str());
        return false;
    }
    return true;
}

bool create_sequence_folder(const std::string& path)
{
    std::error_code error;
    std::filesystem::create_directories(sequence_folder(path), error);
    if (error)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Create capture sequence folder '%s' failed: %s", path.c_str(), error.message().c_str());
        return false;
    }
    return true;
}

struct SdlCleanup final
{
    SdlCleanup() = default;
    SdlCleanup(const SdlCleanup&) = delete;
    SdlCleanup& operator=(const SdlCleanup&) = delete;

    ~SdlCleanup()
    {
        SDL_Quit();
    }
};

struct FrameHistory
{
    std::array<float, 240> milliseconds{};
    std::size_t count = 0;
    std::size_t next = 0;
    void add(double value)
    {
        milliseconds[next] = static_cast<float>(value);
        next = (next + 1) % milliseconds.size();
        count = std::min(count + 1, milliseconds.size());
    }
    std::span<const float> samples() const { return {milliseconds.data(), count}; }
    int offset() const { return count < milliseconds.size() ? 0 : static_cast<int>(next); }
};

using pyroclast::Sandbox;

std::string statistics_json(const std::optional<pyroclast::FrameStatistics>& statistics)
{
    if (!statistics) return "null";
    char text[224];
    std::snprintf(text, sizeof(text), "{\"average\":%.6f,\"median\":%.6f,\"p95\":%.6f,\"p99\":%.6f,\"worst_1_percent\":%.6f,\"max\":%.6f}",
        statistics->average, statistics->median, statistics->p95, statistics->p99, statistics->worst_1_percent, statistics->max);
    return text;
}

const char* pose_name(const pyroclast::ApplicationOptions& options)
{
    return options.camera ? "custom" : pyroclast::view_name(options.view);
}

std::string view_keys(float vertical_fov, bool borderless)
{
    std::string keys;
    if (vertical_fov != pyroclast::Camera{}.vertical_fov)
    {
        char text[48];
        std::snprintf(text, sizeof(text), ",\"fov\":%.3f", static_cast<double>(vertical_fov) * 57.29577951308232);
        keys += text;
    }
    if (borderless) keys += ",\"borderless\":true";
    return keys;
}

class ScenarioRun final
{
public:
    ScenarioRun(pyroclast::Scenario scenario, std::string benchmark, std::uint32_t warmup_frames) :
        scenario_(std::move(scenario)), benchmark_(std::move(benchmark)), warmup_left_(warmup_frames)
    {
    }
    explicit ScenarioRun(pyroclast::Scenario scenario) : ScenarioRun(std::move(scenario), {}, 0) {}
    bool finished(const Sandbox& sandbox) const { return sandbox.particles.tick() >= scenario_.duration_ticks; }
    void invalidate(const char* reason)
    {
        if (valid_)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Scenario measurement invalidated: %s.", reason);
            invalid_reason_ = reason;
        }
        valid_ = false;
    }
    const std::string& benchmark() const { return benchmark_; }
    const char* invalid_reason() const { return invalid_reason_; }
    bool warming() const { return warmup_left_ != 0; }
    bool end_warmup_frame() { return warmup_left_ != 0 && --warmup_left_ == 0; }
    void skipped()
    {
        if (benchmark_.empty() || warming()) return;
        ++skipped_frames_;
        invalidate("a frame was skipped while measuring");
    }
    std::size_t skipped_frames() const { return skipped_frames_; }
    void swapchain_rebuilt()
    {
        if (benchmark_.empty() || warming()) return;
        invalidate("the swapchain was rebuilt while measuring");
    }
    const pyroclast::Scenario& script() const { return scenario_; }
    void set_vertical_fov(float radians) { vertical_fov_ = radians; }
    void submit_due(Sandbox& sandbox)
    {
        while (next_ < scenario_.events.size() && scenario_.events[next_].tick <= sandbox.particles.tick())
        {
            sandbox.effects.submit(scenario_.events[next_++].event);
        }
    }
    void record(double cpu_ms, std::optional<pyroclast::GpuTimings> gpu, const Sandbox& sandbox)
    {
        cpu_ms_.push_back(cpu_ms);
        if (gpu && gpu->sample_id != last_gpu_sample_)
        {
            last_gpu_sample_ = gpu->sample_id;
            draw_ms_.push_back(gpu->draw_ms);
            compute_ms_.push_back(gpu->compute_ms);
            for (std::uint32_t index = 0; index < gpu->region_count; ++index)
            {
                const auto& region = gpu->regions[index];
                auto found = std::find_if(regions_.begin(), regions_.end(), [&region](const RegionSum& sum) { return sum.label == region.label; });
                if (found == regions_.end()) found = regions_.insert(regions_.end(), {region.label, 0, 0});
                found->milliseconds += region.milliseconds;
                ++found->samples;
            }
        }
        peak_effects_ = std::max(peak_effects_, static_cast<std::uint32_t>(sandbox.effects.explosions()));
    }
    void count(const pyroclast::ParticleCounters& counters)
    {
        ++counter_samples_;
        peak_alive_ = std::max(peak_alive_, counters.alive);
        alive_sum_ += counters.alive;
        checksum_ = pyroclast::counter_checksum(checksum_, counters);
    }
    int report(const Sandbox& sandbox, const pyroclast::ApplicationOptions& options, std::string_view gpu_name, int width, int height,
        bool overlay, const std::string& extra, const pyroclast::CounterLog::Summary* summary = nullptr, const std::string& tail = {})
    {
        const auto statistics = [](std::vector<double>& samples) {
            if (samples.empty()) return std::string("null");
            const auto summary = pyroclast::summarize(samples);
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
        std::string camera = options.camera ? ",\"camera\":" + pyroclast::camera_json(*options.camera) : std::string();
        camera += view_keys(vertical_fov_, options.borderless);
        if (!extra.empty()) camera += "," + extra;
        std::printf("{%s\"scenario\":\"%.*s\",\"valid\":%s,\"build\":\"%s\",\"renderer\":\"%s\",\"gpu\":%s,\"width\":%d,\"height\":%d,"
            "\"smoke_resolution\":\"%s\",\"scene_format\":\"%s\",\"view\":\"%s\"%s,\"overlay\":%s,\"effect_lines\":%s,\"gpu_regions\":%s,"
            "\"preset\":\"%.*s\",\"layer_mask\":%u,\"wind\":[%.2f,%.1f,%.2f],\"ticks\":%u,\"frames\":%zu,\"effects_started\":%llu,\"effects_dropped\":%llu,\"effects_evicted\":%llu,\"peak_effects\":%u,"
            "\"particles\":{\"samples\":%zu,\"peak_alive\":%u,\"mean_alive\":%.3f,\"checksum\":\"%016llx\"},\"cpu_ms\":%s,"
            "\"gpu_ms\":{\"samples\":%zu,\"draw\":%s,\"compute\":%s,\"regions\":[%s]}%s}\n",
            benchmark_key.c_str(), static_cast<int>(scenario_.name.size()), scenario_.name.data(), valid_ ? "true" : "false",
            pyroclast::build_configuration,
            pyroclast::renderer_name(options.renderer), gpu.c_str(), width, height, pyroclast::smoke_resolution_name(options.scalability.smoke_resolution),
            pyroclast::scene_format_name(options.scene_format), pose_name(options), camera.c_str(), overlay ? "true" : "false",
            sandbox.views.effect_bounds ? "true" : "false", options.gpu_regions ? "true" : "false",
            static_cast<int>(pyroclast::preset_name(options.preset).size()), pyroclast::preset_name(options.preset).data(),
            sandbox.layer_mask, static_cast<double>(sandbox.wind.speed), static_cast<double>(sandbox.wind.direction_degrees),
            static_cast<double>(sandbox.wind.gustiness), scenario_.duration_ticks, frames,
            static_cast<unsigned long long>(sandbox.effects.started()), static_cast<unsigned long long>(sandbox.effects.dropped()),
            static_cast<unsigned long long>(sandbox.effects.evicted()), peak_effects, counter_samples, peak_alive, mean_alive,
            static_cast<unsigned long long>(checksum), cpu.c_str(), gpu_samples, draw.c_str(), compute.c_str(), regions.c_str(), tail.c_str());
        std::fflush(stdout);
        return valid_ ? 0 : 1;
    }

private:
    pyroclast::Scenario scenario_;
    std::size_t next_ = 0;
    std::vector<double> cpu_ms_;
    std::vector<double> draw_ms_;
    std::vector<double> compute_ms_;
    std::uint64_t last_gpu_sample_ = 0;
    struct RegionSum
    {
        const char* label;
        double milliseconds;
        std::size_t samples;
    };
    std::vector<RegionSum> regions_;
    std::uint32_t peak_effects_ = 0;
    std::size_t counter_samples_ = 0;
    std::uint32_t peak_alive_ = 0;
    std::uint64_t alive_sum_ = 0;
    std::uint64_t checksum_ = pyroclast::checksum_basis;
    bool valid_ = true;
    const char* invalid_reason_ = nullptr;
    std::string benchmark_;
    std::uint32_t warmup_left_ = 0;
    std::size_t skipped_frames_ = 0;
    float vertical_fov_ = pyroclast::Camera{}.vertical_fov;
};

std::string benchmark_tail(const ScenarioRun& run, const pyroclast::CounterLog::Summary& summary, const pyroclast::GraphicsDevice& device,
    pyroclast::Renderer renderer, bool gpu_validation, std::optional<float> display_hz, bool focused,
    const std::optional<pyroclast::GpuMemory>& memory_start, const std::optional<pyroclast::GpuMemory>& memory_end)
{
    const auto memory_json = [](const pyroclast::GpuMemory& memory) {
        char text[192];
        std::snprintf(text, sizeof(text), "{\"local_usage\":%llu,\"local_budget\":%llu,\"non_local_usage\":%llu,\"non_local_budget\":%llu}",
            static_cast<unsigned long long>(memory.local_usage), static_cast<unsigned long long>(memory.local_budget),
            static_cast<unsigned long long>(memory.non_local_usage), static_cast<unsigned long long>(memory.non_local_budget));
        return std::string(text);
    };
    const auto count_json = [](const std::optional<pyroclast::CounterLog::CountSummary>& count) {
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
        pyroclast::benchmark_warmup_frames, run.skipped_frames(), focused ? "true" : "false", gpu_validation ? "true" : "false",
        pyroclast::ParticleSystem::capacity,
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
        tail += std::string("{\"source\":\"") + (renderer == pyroclast::Renderer::d3d12 ? "dxgi" : "vk_ext_memory_budget") +
            "\",\"start\":" + memory_json(*memory_start) + ",\"end\":" + memory_json(*memory_end) + "}";
    }
    else tail += "null";
    return tail;
}

struct FlightControls
{
    bool looking = false;
    pyroclast::float2 look;

    void stop_looking(SDL_Window* window)
    {
        if (looking) SDL_SetWindowRelativeMouseMode(window, false);
        looking = false;
    }
    pyroclast::CameraInput input(bool keyboard)
    {
        pyroclast::CameraInput result;
        if (keyboard)
        {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            const auto axis = [keys](SDL_Scancode positive, SDL_Scancode negative) {
                return static_cast<float>(keys[positive]) - static_cast<float>(keys[negative]);
            };
            result.move = {axis(SDL_SCANCODE_D, SDL_SCANCODE_A), axis(SDL_SCANCODE_E, SDL_SCANCODE_Q), axis(SDL_SCANCODE_W, SDL_SCANCODE_S)};
            result.fast = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
        }
        result.look = look;
        look = {};
        return result;
    }
};

pyroclast::Camera camera_pose(const std::array<float, 6>& pose)
{
    pyroclast::Camera camera;
    camera.position = {pose[0], pose[1], pose[2]};
    camera.look_at({pose[3], pose[4], pose[5]});
    return camera;
}

int run_event_loop(SDL_Window* window, pyroclast::GraphicsDevice& renderer, Sandbox& sandbox, pyroclast::DebugUi* ui,
    const pyroclast::ApplicationOptions& options, std::optional<ScenarioRun> scenario, pyroclast::CounterLog* counter_log)
{
    const auto window_id = SDL_GetWindowID(window);
    auto& demo = sandbox.particles;
    {
        std::array<pyroclast::Camera, pyroclast::CameraRig::bookmark_count> views;
        std::array<std::string_view, pyroclast::CameraRig::bookmark_count> names;
        for (std::size_t index = 0; index < views.size(); ++index)
        {
            const auto view = static_cast<pyroclast::RangeView>(index);
            views[index] = sandbox.range.view(view);
            names[index] = pyroclast::view_name(view);
        }
        sandbox.camera = pyroclast::CameraRig(options.camera ? camera_pose(*options.camera) : sandbox.range.view(options.view), views, names);
    }
    const float start_fov = sandbox.camera.camera().vertical_fov;
    if (scenario) scenario->set_vertical_fov(start_fov);
    const auto window_size = options.window;
    const std::string size_reason = "the window is not " + std::to_string(window_size.width) + "x" + std::to_string(window_size.height);
    FlightControls flight;
    const auto profile_tick = options.profile_tick;
    const bool frozen = scenario && profile_tick;
    const bool unattended = scenario.has_value() || !options.capture_path.empty();
    pyroclast::FrameBaseline baseline({pyroclast::renderer_name(options.renderer), std::string(renderer.adapter_name()),
        static_cast<std::uint32_t>(sandbox.group.size()), pyroclast::ParticleSystem::capacity, profile_tick,
        pyroclast::smoke_resolution_name(options.scalability.smoke_resolution),
        pose_name(options), options.camera, frozen ? options.scenario : std::string(), pyroclast::scene_format_name(options.scene_format),
        std::string(pyroclast::preset_name(options.preset)), options.layer_mask, options.wind, window_size.width, window_size.height,
        view_keys(start_fov, options.borderless)});
    if (scenario && !frozen) baseline.disable();
    if (ui)
    {
        baseline.disable();
        SDL_Log("Startup sample skipped while the overlay is on; launch with --ui=off to take it.");
    }
    FrameHistory history;
    FrameHistory gpu_history;
    std::uint64_t last_gpu_sample = 0;
    pyroclast::TickRate tick_rate;
    std::uint32_t last_steps = 0;
    double showcase_ms = 0;
    pyroclast::CoverageStatistics coverage;
    const bool automatic = options.scalability.smoke_resolution == pyroclast::SmokeResolution::automatic;
    const bool realism = pyroclast::realism_enabled(options.layer_mask);
    std::uint32_t peak_clusters = 0;
    std::uint32_t peak_residual = 0;
    std::uint32_t ghosts = 0;
    std::deque<std::pair<std::uint32_t, std::uint32_t>> rotor_expected;
    std::uint32_t rotor_mismatches = 0;
    std::uint32_t peak_dust = 0;
    std::int32_t lowest_hub_cm = std::numeric_limits<std::int32_t>::max();
    std::pair<std::uint32_t, std::uint32_t> rotor_slot{pyroclast::never, pyroclast::never};
    std::uint32_t peak_knots = 0;
    std::pair<std::uint32_t, std::uint32_t> jet_slot{pyroclast::never, pyroclast::never};
    std::uint32_t peak_jet_dust = 0;
    const auto scalability_json = [&coverage, &sandbox, automatic, realism, &peak_clusters, &peak_residual, &ghosts, &rotor_mismatches, &peak_dust,
                                      &lowest_hub_cm, &rotor_slot, &peak_knots, &jet_slot, &peak_jet_dust]() {
        std::string text = coverage.json();
        if (automatic)
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
                pyroclast::SmokeResolutionPolicy::limit(settings.smoke_budget));
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
        if (settings.effect_order != pyroclast::EffectOrder::origin)
        {
            add(std::string("\"effect_order\":\"") + pyroclast::effect_order_name(settings.effect_order) + "\"");
        }
        if (settings.slot_policy != pyroclast::SlotPolicy::drop_new)
        {
            add(std::string("\"slot_policy\":\"") + pyroclast::slot_policy_name(settings.slot_policy) + "\"");
        }
        if (!options.empty()) text += (text.empty() ? "" : ",") + std::string("\"scalability\":{") + options + "}";
        if (realism)
        {
            char lingering[256];
            std::snprintf(lingering, sizeof(lingering),
                "\"realism\":{\"linger_seconds\":%u,\"humidity_percent\":%u,\"forced_fades\":%llu,\"deferred\":%llu,\"peak_clusters\":%u,"
                "\"peak_residual\":%u,\"ghosts\":%u}",
                sandbox.linger.linger_ticks / 60, static_cast<unsigned>(std::lround(sandbox.humidity * 100)),
                static_cast<unsigned long long>(sandbox.effects.residual().forced_fades()),
                static_cast<unsigned long long>(sandbox.effects.deferred()), peak_clusters, peak_residual, ghosts);
            text += (text.empty() ? "" : ",") + std::string(lingering);
            if (const auto& helicopter = sandbox.rotorcraft.helicopter())
            {
                char flight[320];
                std::snprintf(flight, sizeof(flight),
                    ",\"helicopter\":{\"flight\":\"%s\",\"listed\":[%u,%u],\"rotor_slot\":[%u,%u],\"peak_dust\":%u,\"lowest_hub_cm\":%d,\"mismatches\":%u}",
                    helicopter->plan.name.c_str(), helicopter->plan.list_tick, helicopter->plan.unlist_tick, rotor_slot.first, rotor_slot.second, peak_dust,
                    lowest_hub_cm == std::numeric_limits<std::int32_t>::max() ? -1 : lowest_hub_cm, rotor_mismatches);
                text += flight;
            }
            if (const auto& jet = sandbox.jetcraft.flight())
            {
                char passes[256];
                std::snprintf(passes, sizeof(passes),
                    ",\"jet\":{\"flight\":\"%s\",\"passes\":%zu,\"peak_knots\":%u,\"jet_slot\":[%u,%u],\"peak_dust\":%u,\"mismatches\":%u}",
                    jet->name.c_str(), jet->passes.size(), peak_knots, jet_slot.first, jet_slot.second, peak_jet_dust, rotor_mismatches);
                text += passes;
            }
        }
        return text;
    };
    const auto record_json = [&scalability_json, &renderer]() {
        const auto mode = renderer.present_info().mode;
        auto text = scalability_json();
        return text + (text.empty() ? "" : ",") + "\"present_mode\":\"" + std::string(mode) + "\"";
    };
    baseline.set_extra(record_json);
    std::vector<pyroclast::UiCommand> commands;
    auto previous_time = SDL_GetPerformanceCounter();
    const auto run_start = previous_time;
    const std::uint32_t sequence_first = options.capture_from.value_or(1);
    const std::uint32_t sequence_last = scenario ? options.capture_to.value_or(scenario->script().duration_ticks) : 0;
    std::uint32_t sequence_frames = 0;
    int sequence_width = 0;
    int sequence_height = 0;
    const double counter_milliseconds = 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
    std::optional<pyroclast::GpuMemory> memory_start;
    std::optional<float> display_hz;
    bool focused = true;
    std::uint32_t submitted_frames = 0;
    const auto finish = [&options, &scenario, frozen]() {
        if (scenario && !frozen)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Exited before scenario %s finished; no summary.", options.scenario.c_str());
            return 1;
        }
        if (options.capture_path.empty()) return 0;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Exited before the capture at tick %u was drawn.",
            options.capture_tick ? *options.capture_tick : *options.profile_tick);
        return 1;
    };
    while (true)
    {
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                    event.window.windowID == window_id))
            {
                return finish();
            }
            if ((event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT) ||
                event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
            {
                flight.stop_looking(window);
            }
            if (ui && ui->process_event(event)) continue;
            if (ui && event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F1 && !event.key.repeat) ui->toggle_visible();
            if (event.type == SDL_EVENT_WINDOW_MINIMIZED || event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
                (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && (event.window.data1 != static_cast<int>(window_size.width) ||
                    event.window.data2 != static_cast<int>(window_size.height))))
            {
                baseline.disable();
                if (scenario) scenario->invalidate("the window was minimized, unfocused or resized");
            }
            if (unattended) continue;
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.windowID == window_id && event.button.button == SDL_BUTTON_RIGHT)
            {
                flight.looking = SDL_SetWindowRelativeMouseMode(window, true);
                baseline.disable();
            }
            if (event.type == SDL_EVENT_MOUSE_MOTION && flight.looking)
            {
                flight.look.x += event.motion.xrel;
                flight.look.y += event.motion.yrel;
            }
            if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.windowID == window_id)
            {
                if (sandbox.pilot.controlling() && sandbox.pilot.chase().attached()) sandbox.pilot.chase().zoom(event.wheel.y);
                else sandbox.camera.scale_speed(event.wheel.y);
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.windowID == window_id &&
                event.button.button == SDL_BUTTON_LEFT && flight.looking)
            {
                sandbox.spawn(pyroclast::crosshair_ray(sandbox.camera.camera()), "crosshair");
                baseline.disable();
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.windowID == window_id &&
                event.button.button == SDL_BUTTON_LEFT)
            {
                int window_width = 0;
                int window_height = 0;
                if (SDL_GetWindowSize(window, &window_width, &window_height) && window_width > 0 && window_height > 0)
                {
                    const float w = static_cast<float>(window_width);
                    const float h = static_cast<float>(window_height);
                    const auto ndc = pyroclast::window_to_ndc(event.button.x, event.button.y, w, h);
                    sandbox.spawn(sandbox.camera.camera().ray_through(ndc.x, ndc.y, w / h), "cursor");
                    baseline.disable();
                }
            }
            if (event.type == SDL_EVENT_KEY_DOWN) baseline.disable();
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.windowID == window_id && !event.key.repeat)
            {
                if (const auto command = pyroclast::key_command(event.key.key, event.key.mod))
                {
                    pyroclast::apply(*command, sandbox);
                    if (command->kind == pyroclast::UiCommand::Kind::recall_bookmark) flight.look = {};
                }
            }
        }

        const bool minimized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0;
        if (!minimized)
        {
            int width = 0;
            int height = 0;
            if (!SDL_GetWindowSizeInPixels(window, &width, &height))
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "Read window pixel size failed: %s", SDL_GetError());
                return 1;
            }
            if (width > 0 && height > 0)
            {
                if (width != static_cast<int>(window_size.width) || height != static_cast<int>(window_size.height))
                {
                    baseline.disable();
                    if (scenario) scenario->invalidate(size_reason.c_str());
                }
                const auto start = SDL_GetPerformanceCounter();
                const auto rebuilds = renderer.swapchain_rebuilds();
                if (!renderer.resize(width, height))
                {
                    return 1;
                }
                if (scenario && renderer.swapchain_rebuilds() != rebuilds) scenario->swapchain_rebuilt();
                const double elapsed = static_cast<double>(start - previous_time) /
                    static_cast<double>(SDL_GetPerformanceFrequency());
                previous_time = start;
                const auto input = flight.input(!unattended && (!ui || !ui->wants_keyboard()));
                if (!sandbox.pilot.controlling())
                {
                    if (sandbox.camera.update(input, static_cast<float>(elapsed), sandbox.range.environment(), sandbox.props_shown()))
                    {
                        baseline.disable();
                    }
                }
                else if (!sandbox.pilot.chase().attached())
                {
                    sandbox.camera.update({.look = input.look}, static_cast<float>(elapsed), sandbox.range.environment(), sandbox.props_shown());
                }
                const auto showcase_start = SDL_GetPerformanceCounter();
                sandbox.update_aim((ui && ui->visible()) || sandbox.views.spawn_regions || sandbox.views.wind_arrow);
                auto showcase_ticks = SDL_GetPerformanceCounter() - showcase_start;
                ImDrawData* overlay = nullptr;
                if (ui)
                {
                    pyroclast::DiagnosticsSnapshot snapshot;
                    snapshot.system = {.renderer = pyroclast::renderer_name(options.renderer), .adapter = renderer.adapter_name(),
                        .build = pyroclast::build_configuration, .width = width, .height = height,
                        .vsync = !profile_tick.has_value() && !scenario, .scene_format = pyroclast::scene_format_name(options.scene_format),
                        .exposure = pyroclast::TestRange::exposure};
                    snapshot.frame = {.cpu_ms = history.samples(), .cpu_offset = history.offset(), .gpu_ms = gpu_history.samples(),
                        .gpu_offset = gpu_history.offset(), .gpu = renderer.completed_gpu_timings(), .showcase_cpu_ms = showcase_ms};
                    sandbox.fill_snapshot(snapshot);
                    snapshot.clock.steps = last_steps;
                    snapshot.clock.ticks_per_second = static_cast<float>(tick_rate.rate());
                    commands.clear();
                    overlay = ui->build(snapshot, commands);
                    if (!unattended) for (const auto command : commands) pyroclast::apply(command, sandbox);
                }
                const auto release_start = SDL_GetPerformanceCounter();
                if (scenario && (!frozen || demo.has_pending_steps()) && !scenario->warming()) scenario->submit_due(sandbox);
                else if (!unattended)
                {
                    sandbox.release_due();
                    sandbox.fly_pilot(input, elapsed);
                }
                sandbox.build_marks();
                showcase_ticks += SDL_GetPerformanceCounter() - release_start;
                showcase_ms = 1000.0 * static_cast<double>(showcase_ticks) / static_cast<double>(SDL_GetPerformanceFrequency());
                const auto tick_before = demo.tick();
                const auto started_before = sandbox.effects.started();
                const auto dropped_before = sandbox.effects.dropped();
                const auto evicted_before = sandbox.effects.evicted();
                const bool warming = scenario && scenario->warming();
                const bool sequence_frame = !options.capture_sequence.empty() && !warming && demo.tick() + 1 >= sequence_first &&
                    demo.tick() + 1 <= sequence_last;
                const bool capture = sequence_frame || (!options.capture_path.empty() && !warming &&
                    (options.capture_tick ? demo.tick() + 1 == *options.capture_tick : !demo.has_pending_steps()));
                auto drawn = sandbox.camera.camera();
                if (!unattended && !profile_tick && pyroclast::realism_enabled(sandbox.layer_mask) &&
                    ((sandbox.layer_mask >> pyroclast::realism_layout::shake_mask_bit) & 1) != 0)
                {
                    const float angle = pyroclast::shake_angle(sandbox.effects, sandbox.renderer.blast_classes(), drawn.position, demo.tick());
                    drawn.pitch = std::clamp(drawn.pitch + angle, -1.55f, 1.55f);
                    drawn.yaw += 0.6f * angle;
                }
                const auto submit_begin = SDL_GetPerformanceCounter();
                const auto result = sandbox.renderer.render(renderer, demo, sandbox.effects, sandbox.range, drawn, width, height, elapsed,
                    {capture, overlay, sandbox.views, sandbox.layer_mask, sandbox.wind, sandbox.marks, sandbox.scalability, sandbox.counters,
                        sandbox.linger, sandbox.humidity, &sandbox.rotorcraft, &sandbox.jetcraft});
                const auto submit_end = SDL_GetPerformanceCounter();
                if (result == pyroclast::RenderResult::failed) return 1;
                if (!unattended)
                {
                    const double now = static_cast<double>(SDL_GetPerformanceCounter()) / static_cast<double>(SDL_GetPerformanceFrequency());
                    if (const auto report = sandbox.pressure.record(tick_before, now,
                            static_cast<std::uint32_t>(sandbox.effects.explosions()), sandbox.effects.dropped()))
                    {
                        sandbox.report_drops(*report);
                    }
                }
                last_steps = demo.tick() - tick_before;
                if (counter_log && result == pyroclast::RenderResult::submitted)
                {
                    counter_log->add_frame({demo.tick(), last_steps, static_cast<std::uint32_t>(sandbox.effects.explosions()),
                        sandbox.effects.started() - started_before, sandbox.effects.dropped() - dropped_before, sandbox.renderer.particles_drawn(),
                        sandbox.renderer.half_smoke(), sandbox.effects.evicted() - evicted_before,
                        warming ? pyroclast::FramePhase::warmup : pyroclast::FramePhase::measured,
                        static_cast<double>(start - run_start) * counter_milliseconds, static_cast<double>(submit_end - submit_begin) * counter_milliseconds,
                        renderer.last_submit_timings(), sandbox.renderer.submitted_work(), sandbox.renderer.listed_clusters(),
                        sandbox.effects.residual().forced_fades(), sandbox.effects.residual().last_scale(), sandbox.effects.deferred(),
                        sandbox.renderer.rotor_frame().census.alive, sandbox.renderer.rotor_frame().emission,
                        sandbox.renderer.rotor_frame().hub_height_cm});
                }
                if (result == pyroclast::RenderResult::submitted && realism)
                {
                    const auto& rotor = sandbox.renderer.rotor_frame();
                    if (rotor.exists && rotor.listed) lowest_hub_cm = std::min(lowest_hub_cm, rotor.hub_height_cm);
                    peak_dust = std::max(peak_dust, rotor.census.alive);
                    const auto& jet = sandbox.renderer.jet_frame();
                    peak_knots = std::max(peak_knots, jet.wake.count);
                    peak_jet_dust = std::max(peak_jet_dust, jet.census.dust);
                    if (jet.slot)
                    {
                        const auto& slot = sandbox.effects.slots()[*jet.slot];
                        jet_slot = {slot.start_tick, slot.start_tick + slot.lifetime_ticks};
                    }
                    if (rotor.slot)
                    {
                        const auto& slot = sandbox.effects.slots()[*rotor.slot];
                        rotor_slot = {slot.start_tick, slot.start_tick + slot.lifetime_ticks};
                    }
                    if ((rotor.slot || jet.slot) && sandbox.effects.explosions() == 0 && sandbox.renderer.particles_drawn())
                    {
                        const std::uint32_t rotor_records = rotor.slot ? rotor.census.alive + (rotor.listed ? pyroclast::rotor_layout::rotor_disc_records : 0u) : 0u;
                        const std::uint32_t jet_records = jet.slot ? jet.census.dust : 0u;
                        rotor_expected.push_back({demo.tick(), rotor_records + jet_records});
                    }
                }
                const double wall = static_cast<double>(SDL_GetPerformanceCounter()) / static_cast<double>(SDL_GetPerformanceFrequency());
                tick_rate.add(wall, demo.tick());
                if (const auto gpu = renderer.completed_gpu_timings(); gpu && gpu->sample_id != last_gpu_sample)
                {
                    last_gpu_sample = gpu->sample_id;
                    gpu_history.add(gpu->compute_ms + gpu->draw_ms);
                    if (counter_log) counter_log->add_timing(*gpu);
                }
                const auto counters = sandbox.poll_counters(renderer);
                peak_clusters = std::max(peak_clusters, sandbox.renderer.listed_clusters());
                if (counters)
                {
                    peak_residual = std::max(peak_residual, counters->residual);
                    ghosts = std::max(ghosts, counters->ghosts);
                    while (!rotor_expected.empty() && rotor_expected.front().first < counters->tick) rotor_expected.pop_front();
                    if (!rotor_expected.empty() && rotor_expected.front().first == counters->tick)
                    {
                        const auto expected = rotor_expected.front().second;
                        rotor_expected.pop_front();
                        if (counters->smoke - counters->residual != expected)
                        {
                            if (rotor_mismatches == 0)
                            {
                                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Rotor slot at tick %u: the GPU counts %u records, the CPU's twin %u.",
                                    counters->tick, counters->smoke - counters->residual, expected);
                            }
                            ++rotor_mismatches;
                        }
                    }
                }
                if (scenario && !frozen && counters && scenario->benchmark().empty()) scenario->count(*counters);
                if (counter_log && counters) counter_log->add_counters(*counters);
                if (counters && counters->measured && (!profile_tick || counters->tick == *profile_tick))
                {
                    coverage.add(counters->smoke_coverage, counters->light_coverage, counters->offscreen,
                        static_cast<double>(width) * static_cast<double>(height));
                }
                if (result == pyroclast::RenderResult::submitted)
                {
                    history.add(1000.0 * elapsed);
                    const double milliseconds = 1000.0 * static_cast<double>(SDL_GetPerformanceCounter() - start) /
                        static_cast<double>(SDL_GetPerformanceFrequency());
                    if (counter_log) counter_log->set_cpu_ms(milliseconds);
                    if (!options.capture_sequence.empty() && sequence_width == 0)
                    {
                        sequence_width = width;
                        sequence_height = height;
                    }
                    if (warming)
                    {
                        if (scenario->end_warmup_frame())
                        {
                            demo.toggle_pause();
                            memory_start = renderer.memory_usage();
                            focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
                            if (const auto* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window)); mode && mode->refresh_rate > 0)
                            {
                                display_hz = mode->refresh_rate;
                            }
                            SDL_Log("Benchmark %s: %u warm-up frames done; measuring %s (%s, %s), %u ticks.", scenario->benchmark().c_str(),
                                pyroclast::benchmark_warmup_frames, options.scenario.c_str(), pyroclast::preset_name(options.preset).data(),
                                pose_name(options), scenario->script().duration_ticks);
                        }
                        continue;
                    }
                    if (!profile_tick && baseline.enabled() && sandbox.effects.explosions() != sandbox.group.size())
                    {
                        baseline.disable();
                        SDL_Log("Startup sample stopped: the group retired before the sample completed; use --profile-tick or --scenario.");
                    }
                    if (!profile_tick || !demo.has_pending_steps())
                        baseline.record(milliseconds, renderer.completed_gpu_timings(), !sandbox.renderer.particles_drawn() ?
                            std::optional<std::uint32_t>(0) : sandbox.counters ? std::optional(sandbox.counters->alive) : std::nullopt);
                    if (sequence_frame)
                    {
                        if (width != sequence_width || height != sequence_height)
                        {
                            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "The window changed size during the capture sequence.");
                            return 1;
                        }
                        char name[32];
                        std::snprintf(name, sizeof(name), "/tick-%06u.png", demo.tick());
                        if (save_capture(renderer, options.capture_sequence + name, false) != 0) return 1;
                        ++sequence_frames;
                        if (demo.tick() >= sequence_last)
                        {
                            SDL_Log("Capture sequence: %u frames, ticks %u-%u, in %s.", sequence_frames, sequence_first, sequence_last,
                                options.capture_sequence.c_str());
                            return 0;
                        }
                    }
                    else if (capture) return save_capture(renderer, options.capture_path);
                    if (scenario && options.capture_tick && scenario->finished(sandbox))
                    {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Scenario %s ended before tick %u.", options.scenario.c_str(),
                            *options.capture_tick);
                        return 1;
                    }
                    if (scenario && !frozen)
                    {
                        if (scenario->benchmark().empty()) scenario->record(milliseconds, renderer.completed_gpu_timings(), sandbox);
                        if (scenario->finished(sandbox))
                        {
                            if (rotor_mismatches != 0)
                            {
                                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "The rotor slot's counts differed from the CPU's twin in %u frames.", rotor_mismatches);
                            }
                            if (scenario->benchmark().empty())
                            {
                                const int code = scenario->report(sandbox, options, renderer.adapter_name(), width, height, ui != nullptr, record_json());
                                return rotor_mismatches != 0 ? 1 : code;
                            }
                            const auto summary = counter_log->summary();
                            if (counter_log->mismatches() != 0) scenario->invalidate("counter log samples did not match their frames");
                            if (summary.gpu_samples == 0) scenario->invalidate("no GPU timestamps arrived");
                            const auto tail = benchmark_tail(*scenario, summary, renderer, options.renderer, options.gpu_validation, display_hz,
                                focused, memory_start, renderer.memory_usage());
                            const int code = scenario->report(sandbox, options, renderer.adapter_name(), width, height, ui != nullptr, scalability_json(),
                                &summary, tail);
                            return rotor_mismatches != 0 ? 1 : code;
                        }
                    }
                    if (options.exit_after && ++submitted_frames >= *options.exit_after)
                    {
                        SDL_Log("Exiting after %u submitted frames.", submitted_frames);
                        return finish();
                    }
                }
                else
                {
                    baseline.disable();
                    if (scenario) scenario->skipped();
                }
                continue;
            }
        }

        SDL_ClearError();
        if (!SDL_WaitEventTimeout(nullptr, 100) && SDL_GetError()[0] != '\0')
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "Event wait failed: %s", SDL_GetError());
            return 1;
        }
        previous_time = SDL_GetPerformanceCounter();
    }
}

int run_application(const pyroclast::ApplicationOptions& options)
{
    const SdlCleanup sdl_cleanup;
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SDL initialization failed: %s", SDL_GetError());
        return 1;
    }
    if (!options.capture_sequence.empty() && !check_sequence_folder(options.capture_sequence)) return 1;

    const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("Pyroclast", static_cast<int>(options.window.width), static_cast<int>(options.window.height), SDL_WINDOW_RESIZABLE |
            (options.renderer == pyroclast::Renderer::vulkan ? SDL_WINDOW_VULKAN : 0) | (options.borderless ? SDL_WINDOW_BORDERLESS : 0)),
        &SDL_DestroyWindow);
    if (!window)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Window creation failed: %s", SDL_GetError());
        return 1;
    }
    if (options.borderless && !SDL_SetWindowPosition(window.get(), 0, 0))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Move the borderless window failed: %s", SDL_GetError());
    }
    if (options.window != pyroclast::WindowSize{} || options.borderless)
    {
        SDL_Log("Window: %ux%u%s.", options.window.width, options.window.height, options.borderless ? ", borderless" : "");
    }

#ifdef NDEBUG
    if (options.gpu_validation)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "--gpu-validation requires a Debug build.");
        return 1;
    }
#endif
    const bool measured = options.profile_tick.has_value() || !options.scenario.empty();
    std::unique_ptr<pyroclast::DebugUi> ui;
    if (options.ui.value_or(!measured))
    {
        ui = pyroclast::DebugUi::create(window.get(), options.renderer);
        if (!ui) return 1;
    }
    const pyroclast::GraphicsDeviceOptions device_options{
        .gpu_timing = options.gpu_timing || measured || ui != nullptr,
        .vsync = !measured,
        .capture = !options.capture_path.empty() || !options.capture_sequence.empty(),
        .gpu_validation = options.gpu_validation,
        .gpu_regions = options.gpu_regions,
        .adapter = options.adapter,
        .debug_overlay = ui != nullptr};
    auto creation = pyroclast::create_graphics_device(options.renderer, window.get(), device_options);
    if (!creation.device)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", creation.error.c_str());
        return 1;
    }
    auto& device = *creation.device;
    SDL_Log("Smoke resolution: %s; scene target: %s.", pyroclast::smoke_resolution_name(options.scalability.smoke_resolution),
        pyroclast::scene_format_name(options.scene_format));
    const auto scene_format = options.scene_format == pyroclast::SceneFormat::rgba16f ? pyroclast::TargetFormat::rgba16_float :
        options.scene_format == pyroclast::SceneFormat::r11g11b10f ? pyroclast::TargetFormat::r11g11b10_float : pyroclast::TargetFormat::color;
    auto sandbox = std::make_unique<Sandbox>();
    if (!sandbox->range.initialize(device, scene_format) ||
        !sandbox->particles.initialize(device, sandbox->range.environment_buffer(), {scene_format, pyroclast::ExplosionRenderer::smoke_format},
            pyroclast::gpu_effect_classes()) ||
        !sandbox->renderer.initialize(device, scene_format))
    {
        return 1;
    }
    sandbox->views.effect_bounds = options.effect_lines || options.debug_views.effect_bounds;
    sandbox->views.collision_boxes = options.effect_lines || options.debug_views.collision_boxes;
    sandbox->views.spawn_regions = options.debug_views.spawn_regions;
    sandbox->views.wind_arrow = options.debug_views.wind_arrow;
    sandbox->showcase.preset = options.preset;
    sandbox->layer_mask = options.layer_mask;
    sandbox->wind = options.wind;
    sandbox->scalability = options.scalability;
    sandbox->linger.linger_ticks = options.linger_seconds * 60;
    sandbox->humidity = static_cast<float>(options.humidity_percent) / 100.0f;
    const auto preset = pyroclast::preset_name(options.preset);
    SDL_Log("Preset: %.*s; mask 0x%04x; wind %.1f m/s towards %.0f deg, gustiness %.2f.", static_cast<int>(preset.size()), preset.data(),
        options.layer_mask, options.wind.speed, options.wind.direction_degrees, options.wind.gustiness);
    std::optional<ScenarioRun> scenario;
    std::size_t scripted_frames = 0;
    if (!options.scenario.empty())
    {
        const bool lingers = pyroclast::realism_enabled(options.layer_mask) &&
            ((options.layer_mask >> pyroclast::realism_layout::linger_mask_bit) & 1) != 0;
        const auto linger = lingers ? options.linger_seconds * 60 : 0u;
        auto script = pyroclast::make_scenario(options.scenario, sandbox->range.environment(), options.preset, linger);
        if (!script) return 1;
        if (options.profile_tick && *options.profile_tick > script->duration_ticks)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Scenario %s runs %u ticks; --profile-tick=%u is past its end.", options.scenario.c_str(),
                script->duration_ticks, *options.profile_tick);
            return 1;
        }
        SDL_Log("Scenario %s: %zu events over %u ticks, one tick per frame; VSync off; timestamps on.", options.scenario.c_str(),
            script->events.size(), script->duration_ticks);
        if (!options.capture_sequence.empty())
        {
            const auto first = options.capture_from.value_or(1);
            const auto last = options.capture_to.value_or(script->duration_ticks);
            if (last > script->duration_ticks || first > last)
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Scenario %s runs %u ticks; the capture sequence's ticks %u-%u are outside it.",
                    options.scenario.c_str(), script->duration_ticks, first, last);
                return 1;
            }
            SDL_Log("Capture sequence: ticks %u-%u, %u frames, to %s; every captured frame waits for the GPU and writes a PNG, so this "
                "run prints no record.", first, last, last - first + 1, options.capture_sequence.c_str());
        }
        const bool benchmark = !options.benchmark.empty();
        scripted_frames = script->duration_ticks + (benchmark ? pyroclast::benchmark_warmup_frames : 0);
        if (benchmark)
        {
            SDL_Log("Benchmark %s: %s, %.*s, %s; %u warm-up frames at tick 0, then %u ticks, one per frame; VSync off; timestamps on.",
                options.benchmark.c_str(), options.scenario.c_str(), static_cast<int>(pyroclast::preset_name(options.preset).size()),
                pyroclast::preset_name(options.preset).data(), pose_name(options), pyroclast::benchmark_warmup_frames, script->duration_ticks);
        }
        if (script->flight) sandbox->rotorcraft.fly(*script->flight, pyroclast::helicopter_seed);
        if (script->jet)
        {
            sandbox->jetcraft.fly(*script->jet, pyroclast::jet_seed);
            for (const auto& pass : script->jet->passes)
            {
                const auto over = pyroclast::flight_state(pass, (pass.list_tick + pass.unlist_tick) / 2).position;
                SDL_Log("Jet pass: listed %u-%u, over (%.1f, %.1f, %.1f), %.1f m above the ground there.", pass.list_tick, pass.unlist_tick, over.x, over.y,
                    over.z, over.y - static_cast<double>(sandbox->range.terrain().height(static_cast<float>(over.x), static_cast<float>(over.z))));
            }
        }
        scenario.emplace(std::move(*script), options.benchmark, benchmark ? pyroclast::benchmark_warmup_frames : 0);
        if (!options.profile_tick) sandbox->particles.set_fixed_ticks(true);
        if (benchmark) sandbox->particles.toggle_pause();
    }
    else
    {
        sandbox->group = pyroclast::group_events(sandbox->range.terrain(), options.explosions, options.preset);
        sandbox->start_group();
    }
    if (options.profile_tick)
    {
        sandbox->particles.prepare_profile(*options.profile_tick);
        SDL_Log("Fixed-state profile: tick=%u; VSync off; timestamps on; zero integration steps after preparation.", *options.profile_tick);
    }
    const std::string title = "Pyroclast | " + std::string(pyroclast::controls_summary()) + (ui ? " | F1: overlay" : "");
    SDL_SetWindowTitle(window.get(), title.c_str());
    SDL_Log("Explosion demo ready. %.*s.", static_cast<int>(pyroclast::controls_summary().size()), pyroclast::controls_summary().data());
    std::optional<pyroclast::CounterLog> counter_log;
    const bool automatic_smoke = options.scalability.smoke_resolution == pyroclast::SmokeResolution::automatic;
    if (!options.counter_log.empty() || !options.benchmark.empty())
    {
        const auto& settings = options.scalability;
        counter_log.emplace(settings.coverage || automatic_smoke, automatic_smoke, settings.smoke_lod > 0 || settings.faint_floor > 0,
            settings.slot_policy == pyroclast::SlotPolicy::evict_oldest, !options.benchmark.empty(), pyroclast::realism_enabled(options.layer_mask),
            pyroclast::is_helicopter_scenario(options.scenario));
        counter_log->reserve(std::max({scripted_frames, static_cast<std::size_t>(options.exit_after.value_or(0)),
            options.profile_tick ? static_cast<std::size_t>(*options.profile_tick) + 720 : std::size_t{0}}));
    }
    if (!options.capture_sequence.empty() && !create_sequence_folder(options.capture_sequence)) return 1;
    int exit_code = run_event_loop(window.get(), device, *sandbox, ui.get(), options, std::move(scenario), counter_log ? &*counter_log : nullptr);
    if (counter_log && !options.counter_log.empty())
    {
        std::ofstream file(options.counter_log, std::ios::binary);
        file << counter_log->csv();
        if (!file)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Write counter log '%s' failed.", options.counter_log.c_str());
            exit_code = 1;
        }
        else SDL_Log("Counter log: %zu frames written to %s.", counter_log->size(), options.counter_log.c_str());
    }
    if (counter_log)
    {
        if (counter_log->mismatches() != 0)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Counter log: %zu samples or records did not match their frames.", counter_log->mismatches());
            exit_code = 1;
        }
    }
    const bool idle = device.wait_idle();
    sandbox.reset();
    return idle ? exit_code : 1;
}
}

int main(int argc, char** argv)
{
    const auto arguments = std::span<const char* const>(
        argv + 1, static_cast<std::size_t>(argc - 1));
    const auto result = pyroclast::parse_command_line(arguments);
    if (!result.error.empty())
    {
        std::fprintf(stderr, "Error: %s\n", result.error.c_str());
        return 1;
    }
    if (result.options.show_help)
    {
        std::puts(
            "Usage: Pyroclast.exe [--renderer=d3d12|vulkan] [--explosions=1|10|50|100] [--gpu-timing] [--gpu-regions] [--profile-tick=N]\n"
            "                     [--smoke-resolution=full|half|auto] [--ui=on|off] [--gpu-validation]\n"
            "                     [--capture=FILE.png] [--exit-after=N] [--view=overview|ground|hill|aerial|range|yard|far]\n"
            "                     [--scene-format=rgba16f|r11g11b10f|rgba8] [--scenario=NAME] [--capture-tick=N] [--effect-lines] [--help]\n"
            "  --renderer=d3d12   Request Direct3D 12 (default).\n"
            "  --renderer=vulkan  Request Vulkan.\n"
            "  --explosions=N     Simultaneous explosion group (1, 10, 50, 100; default 1).\n"
            "  --profile-tick=N   Freeze at tick 0-480; enable timestamps and disable VSync. With --scenario: prepare the\n"
            "                     scenario to tick N (1 up to its duration), then freeze and sample it the same way.\n"
            "  --smoke-resolution=full|half|auto  Smoke at full or half resolution (default full); auto draws it at half\n"
            "                     resolution while the measured smoke coverage is above a budget.\n"
            "  --smoke-budget=coverage:S|count:N  With auto: half resolution from S screens of smoke coverage (default 1000),\n"
            "                     or from N alive smoke particles.\n"
            "  --half-light       With half or auto: half-resolution frames draw light at half resolution too.\n"
            "  --effect-order=K   Sort effects back to front by origin (default), cloud (the rising cloud's centre) or none.\n"
            "  --slot-policy=P    With every effect slot busy: graduate-oldest (the realism model's default) graduates an older\n"
            "                     lingering effect early and makes the event wait for its slot; drop-new (the legacy model's\n"
            "                     default) drops the event; evict-oldest (legacy model only) replaces the effect that started earliest.\n"
            "  --gpu-timing       Enable GPU timestamp measurements.\n"
            "  --gpu-regions      Also time every dispatch, pass and the overlay (serializes Vulkan GPU work).\n"
            "  --capture=FILE.png Write the first frame drawn at the profile tick, then exit (requires --profile-tick,\n"
            "                     or --scenario with --capture-tick).\n"
            "  --capture-tick=N   With --scenario and --capture: capture the frame that draws tick N, then exit.\n"
            "  --capture-sequence=DIR  With --scenario or --benchmark: write every frame as DIR/tick-NNNNNN.png (DIR new or\n"
            "                     empty), then exit without a record; --capture-from=T and --capture-to=T bound the ticks.\n"
            "  --exit-after=N     Exit after N submitted frames.\n"
            "  --view=NAME        Start the camera at a named pose: overview (default), ground, hill, aerial, range\n"
            "                     (the north yard's props), yard (close to its walls) or far (260 m off, 40 degree view).\n"
            "  --window=WxH       The window's client size in pixels, 640x360 to 7680x4320 (default 1280x720; a benchmark's\n"
            "                     own unless given). Measurements need the window at exactly this size.\n"
            "  --borderless       A window without a frame at the primary display's top-left corner (display-sized windows).\n"
            "  --camera=EX,EY,EZ,TX,TY,TZ  Start the camera at this eye, looking at this target (metres), instead of --view.\n"
            "  --scene-format=F   Scene target: rgba16f (default), r11g11b10f, or rgba8 (a cost reference; bands).\n"
            "  --scenario=NAME    Scripted run, one tick per frame, then a JSON summary: single, grid-10, grid-50,\n"
            "                     grid-100, barrage (80 impacts walking across the range), presets (all seven in a row)\n"
            "                     range (seven explosions of six presets among the north yard's props), bombardment (400\n"
            "                     impacts within 30 m, 20 a second: more than the 128 effect slots hold) or accumulation\n"
            "                     (impacts within 30 m as often as the 128 slots allow, for 30 s of steady state); with the\n"
            "                     realism model: linger-barrage (240 impacts within 30 m, four a second, run for 90 s),\n"
            "                     linger-saturation (1,200 heavy and small-impact impacts, 30 a second, for 50 s), or the\n"
            "                     helicopter over the barrage for 90 s: heli-hover (above the pall, then a low hover beside it),\n"
            "                     heli-pass (a slow pass through the pall) or heli-landing (a landing in its own brownout),\n"
            "                     or a jet over the barrage: jet-pass (down the pall at 30 m, then back low at 15 m) or\n"
            "                     jet-vapor (a fast pass at Mach 0.85 and a pull-up, in humid air: its vapor cone and trails).\n"
            "  --benchmark=NAME   A named scenario, preset and pose, after 120 warm-up frames at tick 0: single,\n"
            "                     simultaneous-10, -50 or -100 (small-he groups), sustained-barrage (artillery-he),\n"
            "                     smoke-field (100 heavy, from the ground), accumulation (artillery-he), or the realism model\n"
            "                     at 1920x1080: linger-barrage (from the overview), linger-far (from the far view), linger-ground\n"
            "                     (from the ground, inside the smoke), heli-hover (heli-hover from the overview) or heli-ground\n"
            "                     (heli-hover from the ground). The first seven keep the legacy model. Prints a JSON\n"
            "                     record with frame, submit, GPU and work statistics; --counter-log adds the per-frame CSV.\n"
            "  --preset=NAME      small-impact, small-he (default), artillery-he, heavy, fuel-fire, dust-blast or stress-test,\n"
            "                     for the startup group, profiles, scenarios and the first click.\n"
            "  --disable=LIST     Hide layers, the lights, smoke features or the props: comma-separated dust, smoke, plume,\n"
            "                     flash, fireball, sparks, debris, lights, curl, vortex, gusts, shading, shadow, emission,\n"
            "                     props (no props drawn, hit or collided with), collision (props drawn, particles pass),\n"
            "                     deflection (props do not turn the air aside); with the realism model also linger (no smoke lingers),\n"
            "                     rise (no rising columns), shear (no wind shear), variation (identical effects), haze, waves (no\n"
            "                     blast-wave refraction), skirt (no dust skirt), condensation (no Wilson shell), push (fronts do\n"
            "                     not move smoke), shake (no camera shake), rotor (the helicopter's air moves nothing and\n"
            "                     raises no dust) or brownout (its rotor raises no dust).\n"
            "  --realism=on|off   The realism model (default on since M25): lingering, rising smoke that fades out instead of\n"
            "                     popping, haze and visible blast waves; off runs the legacy model (M21's frames). Not with\n"
            "                     --benchmark, which fixes its model.\n"
            "  --linger=S         Realism model: lingering smoke lives about S seconds after its explosion (0.87-1.2 times S with\n"
            "                     variation, less when the residual pool is busy), 10-300 (default 75).\n"
            "  --humidity=P       Realism model: the air's relative humidity in percent, 0-100 (default 80); above 70 heavy and\n"
            "                     stress-test charges show a condensation (Wilson) shell, fully from 90.\n"
            "  --wind=S,D,G       Wind speed 0-40 m/s, heading in degrees from +x towards +z, gustiness 0-1 (default 3,0,0.3).\n"
            "  --effect-lines     Start with each effect's origin, normal and reach, and the props' collision boxes,\n"
            "                     drawn as lines (L toggles).\n"
            "  --debug-views=LIST Start with debug views on: comma-separated bounds (effect bounds), boxes (collision\n"
            "                     boxes), regions (where the showcase places events, F8), wind (wind arrow, F9), or all.\n"
            "                     Not with --effect-lines or --scenario.\n"
            "  --smoke-lod=P      Thin smoke particles larger than P pixels in radius (0 < P <= 4096), fading every other one\n"
            "                     out by 2P and compensating its neighbour.\n"
            "  --faint-floor=F    Do not draw smoke particles whose opacity is below F (0 < F < 1).\n"
            "  --coverage         Count each listed particle's screen coverage and whether it is off screen into the\n"
            "                     counter record (records gain \"coverage\"; counter logs gain its columns).\n"
            "  --counter-log=FILE With --scenario or --profile-tick: write a CSV row per frame at exit (tick, steps, effects\n"
            "                     started and dropped, the frame's live counts and GPU intervals).\n"
            "  --gpu-validation   Debug only: D3D12 GPU-based or Vulkan GPU-assisted validation (slow).\n"
            "  --ui=on|off        Dear ImGui overlay (default on; off with --profile-tick, --scenario or --benchmark). F1\n"
            "                     hides it.\n"
            "  --adapter=NAME     Use the GPU whose name contains NAME (case-insensitive); 'warp' is the D3D12 software rasterizer.\n"
            "  --help             Print usage and exit.\n"
            "Both renderers run the same GPU explosion effect.");
        return 0;
    }

    const int version = SDL_GetVersion();
    SDL_Log("Starting Pyroclast (SDL %d.%d.%d, %s build)",
        SDL_VERSIONNUM_MAJOR(version),
        SDL_VERSIONNUM_MINOR(version),
        SDL_VERSIONNUM_MICRO(version),
        pyroclast::build_configuration);
    SDL_Log("Requested renderer: %s", pyroclast::renderer_name(result.options.renderer));
    const int exit_code = run_application(result.options);
    SDL_Log("Pyroclast shutdown (exit code %d)", exit_code);
    return exit_code;
}
