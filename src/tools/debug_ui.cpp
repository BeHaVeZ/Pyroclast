#include "debug_ui.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <string>

namespace pyroclast
{
std::unique_ptr<DebugUi> DebugUi::create(SDL_Window* window, Renderer renderer)
{
    std::unique_ptr<DebugUi> ui(new DebugUi());
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ui->context_ = true;
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ui->platform_ = renderer == Renderer::vulkan ? ImGui_ImplSDL3_InitForVulkan(window) : ImGui_ImplSDL3_InitForD3D(window);
    if (!ui->platform_)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Dear ImGui SDL3 platform backend failed to initialize.");
        return nullptr;
    }
    return ui;
}

DebugUi::~DebugUi()
{
    if (platform_) ImGui_ImplSDL3_Shutdown();
    if (context_) ImGui::DestroyContext();
}

bool DebugUi::process_event(const SDL_Event& event)
{
    ImGui_ImplSDL3_ProcessEvent(&event);
    if (!visible_) return false;
    const auto& io = ImGui::GetIO();
    const bool keyboard = event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP || event.type == SDL_EVENT_TEXT_INPUT;
    const bool mouse = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
        event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_WHEEL;
    return (keyboard && io.WantCaptureKeyboard) || (mouse && io.WantCaptureMouse);
}

bool DebugUi::wants_keyboard() const
{
    return visible_ && ImGui::GetIO().WantCaptureKeyboard;
}

namespace
{
void row(const char* name, const char* format, auto... values)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(name);
    ImGui::TableNextColumn();
    ImGui::Text(format, values...);
}

int length(std::string_view text)
{
    return static_cast<int>(text.size());
}

void plot(const char* label, const char* id, std::span<const float> samples, int offset)
{
    if (samples.empty()) return;
    const float average = std::accumulate(samples.begin(), samples.end(), 0.0f) / static_cast<float>(samples.size());
    const auto [low, high] = std::minmax_element(samples.begin(), samples.end());
    ImGui::Text("%s %.2f ms (min %.2f, max %.2f, last %zu)", label, average, *low, *high, samples.size());
    ImGui::PlotLines(id, samples.data(), static_cast<int>(samples.size()), offset, nullptr, 0.0f, std::max(*high, 1.0f), ImVec2(340, 40));
}

constexpr ImGuiWindowFlags panel_flags = ImGuiWindowFlags_AlwaysAutoResize;
constexpr float panel_width = 380;

void engine_window(const DiagnosticsSnapshot& snapshot)
{
    const auto& system = snapshot.system;
    const auto& frame = snapshot.frame;
    const auto& clock = snapshot.clock;
    const auto& load = snapshot.load;
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(panel_width, 0), ImVec2(panel_width + 60, ImGui::GetIO().DisplaySize.y - 24));
    ImGui::Begin("Pyroclast (F1 hides)", nullptr, panel_flags);
    if (ImGui::BeginTable("system", 2, ImGuiTableFlags_SizingStretchProp))
    {
        row("Renderer", "%.*s (%.*s)", length(system.renderer), system.renderer.data(), length(system.build), system.build.data());
        row("GPU", "%.*s", length(system.adapter), system.adapter.data());
        row("Resolution", "%d x %d, VSync %s", system.width, system.height, system.vsync ? "on" : "off");
        row("Scene target", "%.*s, exposure %.2f", length(system.scene_format), system.scene_format.data(), system.exposure);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Frame");
    plot("CPU frame interval", "##cpu", frame.cpu_ms, frame.cpu_offset);
    plot("GPU compute + draw", "##gpu", frame.gpu_ms, frame.gpu_offset);
    if (frame.gpu)
    {
        const auto& gpu = *frame.gpu;
        ImGui::Text("GPU compute interval %.3f ms, draw interval %.3f ms", gpu.compute_ms, gpu.draw_ms);
        if (gpu.region_count != 0 && ImGui::BeginTable("regions", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
        {
            for (std::uint32_t index = 0; index < gpu.region_count; ++index) row(gpu.regions[index].label, "%.3f ms", gpu.regions[index].milliseconds);
            ImGui::EndTable();
        }
    }
    else
    {
        ImGui::TextDisabled("No GPU timestamp sample yet");
    }
    ImGui::Text("Showcase CPU %.2f ms (aim, impacts, marks)", frame.showcase_cpu_ms);

    ImGui::SeparatorText("Clock");
    if (ImGui::BeginTable("clock", 2, ImGuiTableFlags_SizingStretchProp))
    {
        row("Tick", "%u (%.1f s simulated)", clock.tick, static_cast<double>(clock.tick) / 60.0);
        row("State", "%s", clock.paused ? "paused" : clock.time_scale == 1 ? "running 1x" : "running 0.25x");
        row("Steps last frame", "%u", clock.steps);
        row("Simulation rate", "%.1f ticks/s (60 is real time)", static_cast<double>(clock.ticks_per_second));
        row("Capped frames", "%llu (catch-up clipped at 4 steps)", static_cast<unsigned long long>(clock.capped_frames));
        ImGui::EndTable();
    }

    if (ImGui::CollapsingHeader("Load", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const float used = load.effect_slots != 0 ? static_cast<float>(load.active_effects) / static_cast<float>(load.effect_slots) : 0.0f;
        const ImVec4 colour = load.active_effects >= load.effect_slots ? ImVec4(0.85f, 0.2f, 0.2f, 1) :
            used >= 0.75f ? ImVec4(0.9f, 0.65f, 0.15f, 1) : ImVec4(0.3f, 0.7f, 0.3f, 1);
        char slots[64];
        std::snprintf(slots, sizeof(slots), "%u of %u effect slots", load.active_effects, load.effect_slots);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, colour);
        ImGui::ProgressBar(used, ImVec2(panel_width - 16, 0), slots);
        ImGui::PopStyleColor();
        if (ImGui::BeginTable("load", 2, ImGuiTableFlags_SizingStretchProp))
        {
            row("Startup group", "%u (Space)", load.explosions);
            row("Peak since clear", "%u, %zu queued", load.peak_since_clear, load.queued);
            row("Started, dropped", "%llu, %llu", static_cast<unsigned long long>(load.started), static_cast<unsigned long long>(load.dropped));
            if (load.slot_policy == "evict-oldest") row("Evicted", "%llu (%.*s)", static_cast<unsigned long long>(load.evicted), length(load.slot_policy), load.slot_policy.data());
            if (load.slot_policy == "graduate-oldest") row("Deferred", "%llu, %zu waiting (graduate-oldest)", static_cast<unsigned long long>(load.deferred), load.waiting);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Dropped");
            ImGui::TableNextColumn();
            if (load.dropped_recent != 0) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1));
            ImGui::Text("%llu since clear, %llu last 60 ticks", static_cast<unsigned long long>(load.dropped_since_clear),
                static_cast<unsigned long long>(load.dropped_recent));
            if (load.dropped_recent != 0) ImGui::PopStyleColor();
            for (std::uint32_t index = 0; index < preset_count; ++index)
            {
                if (load.active_by_preset[index] == 0) continue;
                const auto name = preset_name(static_cast<ExplosionPreset>(index));
                row("  active", "%u %.*s", load.active_by_preset[index], length(name), name.data());
            }
            row("Simulated records", "%u of %u", load.simulated_records, load.pool_capacity);
            if (load.particles)
            {
                const auto& counts = *load.particles;
                row("Alive particles", "%u (%.0f%% of simulated)", counts.alive,
                    load.simulated_records != 0 ? 100.0 * counts.alive / load.simulated_records : 0.0);
                row("  smoke/light/debris", "%u, %u, %u (tick %u)", counts.smoke, counts.light, counts.debris, counts.tick);
            }
            else row("Alive particles", "none");
            if (load.coverage && load.particles && system.width > 0 && system.height > 0)
            {
                const auto& counts = *load.particles;
                const double screens = particle_layout::coverage_unit_pixels / (static_cast<double>(system.width) * system.height);
                const auto& bins = counts.smoke_coverage;
                row("Smoke coverage", "%.2f screens", (bins[0] + bins[1] + bins[2] + bins[3]) * screens);
                row("  by radius", "<4: %.2f, <16: %.2f, <64: %.2f, more: %.2f px", bins[0] * screens, bins[1] * screens,
                    bins[2] * screens, bins[3] * screens);
                row("Light coverage", "%.2f screens", counts.light_coverage * screens);
                row("Off screen", "%u of %u listed", counts.offscreen, counts.smoke + counts.light);
                if (load.smoke_lod > 0 || load.faint_floor > 0) row("Thinned, faint", "%u, %u", counts.thinned, counts.faint);
            }
            if (load.realism)
            {
                row("Residual records", "%u of %u", load.particles ? load.particles->residual : 0u, realism_layout::residual_records);
                row("Clusters", "%u of %u", load.clusters, realism_layout::maximum_clusters);
                row("Linger", "%u s, scale %.2f, %llu forced fades", load.linger_seconds, load.linger_scale / 1024.0,
                    static_cast<unsigned long long>(load.forced_fades));
                if (load.particles && load.particles->ghosts != 0) row("Ghosts", "%u (must be 0)", load.particles->ghosts);
            }
            row("Temporary lights", "%u of %u", load.lights, particle_layout::maximum_lights);
            if (load.smoke_resolution == "auto")
            {
                row("Smoke resolution", "%s (auto: %.0f of %.0f %s, %llu switches)", load.smoke_half ? "half" : "full", load.smoke_load,
                    load.smoke_limit, load.smoke_count_budget ? "particles" : "screens", static_cast<unsigned long long>(load.smoke_switches));
            }
            else row("Smoke resolution", "%.*s", length(load.smoke_resolution), load.smoke_resolution.data());
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void showcase_window(const DiagnosticsSnapshot& snapshot, std::vector<UiCommand>& commands, float& radius_edit, bool& radius_editing,
    float& budget_edit, bool& budget_editing)
{
    const auto& showcase = snapshot.showcase;
    const auto& camera = snapshot.camera;
    const auto& wind = snapshot.wind;
    const auto& views = snapshot.views;
    const auto& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 12, 12), ImGuiCond_FirstUseEver, ImVec2(1, 0));
    ImGui::SetNextWindowSizeConstraints(ImVec2(panel_width, 0), ImVec2(panel_width + 60, io.DisplaySize.y - 24));
    ImGui::Begin("Showcase", nullptr, panel_flags);

    if (showcase.aim)
    {
        const auto& aim = *showcase.aim;
        if (aim.prop.empty())
        {
            ImGui::Text("Crosshair: (%.1f, %.1f, %.1f), %.0f m, on %.*s", aim.point.x, aim.point.y, aim.point.z, aim.distance,
                length(aim.surface), aim.surface.data());
        }
        else
        {
            ImGui::Text("Crosshair: the %.*s (%.*s: %.*s response), %.0f m", length(aim.prop), aim.prop.data(), length(aim.material),
                aim.material.data(), length(aim.surface), aim.surface.data(), aim.distance);
        }
    }
    else ImGui::TextDisabled("Crosshair: nothing to aim at");

    ImGui::SeparatorText("Explosion");
    for (std::uint32_t index = 0; index < preset_count; ++index)
    {
        const auto preset = static_cast<ExplosionPreset>(index);
        const auto label = std::to_string(index + 1) + " " + std::string(preset_name(preset));
        if (index % 3 != 0) ImGui::SameLine();
        if (ImGui::RadioButton(label.c_str(), preset == showcase.preset)) commands.push_back({UiCommand::Kind::select_preset, index});
    }
    for (const std::uint32_t count : {1u, 10u, 50u, 100u})
    {
        if (count != 1) ImGui::SameLine();
        const auto label = "x" + std::to_string(count);
        if (ImGui::RadioButton(label.c_str(), showcase.count == count)) commands.push_back({UiCommand::Kind::set_count, count});
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(C)");
    ImGui::BeginDisabled(!showcase.aim);
    if (ImGui::Button("Fire (F)")) commands.push_back({UiCommand::Kind::fire, 0});
    for (const std::uint32_t count : {1u, 10u, 50u, 100u})
    {
        ImGui::SameLine();
        const auto label = "Spawn " + std::to_string(count);
        if (ImGui::Button(label.c_str())) commands.push_back({UiCommand::Kind::fire, count});
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Stress");
    ImGui::BeginDisabled(!showcase.aim);
    if (ImGui::Button("Barrage (B)")) commands.push_back({UiCommand::Kind::barrage});
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (showcase.barrage_progress)
    {
        const auto done = *showcase.barrage_progress;
        char progress[48];
        std::snprintf(progress, sizeof(progress), "%u of 80, %.1f s left", done, (80 - std::min(done, 80u)) * 15 / 60.0);
        ImGui::ProgressBar(static_cast<float>(done) / 80.0f, ImVec2(-1, 0), progress);
    }
    else ImGui::TextDisabled("80 impacts, 4 a second, across the view");
    ImGui::BeginDisabled(!showcase.aim && !showcase.bombardment_impacts);
    if (ImGui::Button(showcase.bombardment_impacts ? "Stop bombardment (V)" : "Bombardment (V)"))
    {
        commands.push_back({UiCommand::Kind::toggle_bombardment});
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (showcase.bombardment_impacts) ImGui::Text("%u impacts", *showcase.bombardment_impacts);
    else ImGui::TextDisabled("around the crosshair until stopped");
    constexpr std::uint32_t intervals[] = {60, 30, 15, 10, 6, 4, 3, 2, 1};
    const auto rate_label = std::to_string(60 / showcase.bombardment_interval) + " impacts/s";
    ImGui::SetNextItemWidth(130);
    if (ImGui::BeginCombo("Rate", rate_label.c_str()))
    {
        for (const auto interval : intervals)
        {
            const auto label = std::to_string(60 / interval) + " impacts/s";
            if (ImGui::Selectable(label.c_str(), interval == showcase.bombardment_interval))
            {
                commands.push_back({UiCommand::Kind::set_bombardment, interval, {}, showcase.bombardment_radius});
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (!radius_editing) radius_edit = showcase.bombardment_radius;
    ImGui::SetNextItemWidth(110);
    ImGui::SliderFloat("Radius", &radius_edit, 5, 60, "%.0f m", ImGuiSliderFlags_AlwaysClamp);
    radius_editing = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        commands.push_back({UiCommand::Kind::set_bombardment, showcase.bombardment_interval, {}, radius_edit});
    }
    if (showcase.preset_ticks != 0)
    {
        ImGui::TextDisabled("Expected ~%u of %u slots; %.1f impacts/s sustainable", showcase.preset_ticks / showcase.bombardment_interval,
            snapshot.load.effect_slots, static_cast<double>(snapshot.load.effect_slots) * 60.0 / static_cast<double>(showcase.preset_ticks));
    }
    if (ImGui::Button("Group (Space)")) commands.push_back({UiCommand::Kind::trigger});
    ImGui::SameLine();
    if (ImGui::Button("Clear (R)")) commands.push_back({UiCommand::Kind::clear});

    ImGui::SeparatorText("Time");
    if (ImGui::Button(snapshot.clock.paused ? "Resume (P)" : "Pause (P)")) commands.push_back({UiCommand::Kind::toggle_pause});
    ImGui::SameLine();
    ImGui::BeginDisabled(!snapshot.clock.paused);
    if (ImGui::Button("Step (N)")) commands.push_back({UiCommand::Kind::step});
    ImGui::EndDisabled();
    ImGui::SameLine();
    bool slow = snapshot.clock.time_scale != 1;
    if (ImGui::Checkbox("Slow motion 0.25x (T)", &slow)) commands.push_back({UiCommand::Kind::toggle_slow_motion});

    ImGui::SeparatorText("Wind");
    auto settings = wind.settings;
    constexpr auto clamp = ImGuiSliderFlags_AlwaysClamp;
    bool changed = ImGui::SliderFloat("Speed (m/s)", &settings.speed, 0, maximum_wind_speed, "%.1f", clamp);
    changed |= ImGui::SliderFloat("Heading (deg)", &settings.direction_degrees, -180, 180, "%.0f", clamp);
    changed |= ImGui::SliderFloat("Gustiness", &settings.gustiness, 0, 1, "%.2f", clamp);
    if (changed) commands.push_back({UiCommand::Kind::set_wind, 0, settings});
    {
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 corner = ImGui::GetCursorScreenPos();
        constexpr float size = 56;
        const ImVec2 centre(corner.x + size * 0.5f, corner.y + size * 0.5f);
        draw->AddCircle(centre, size * 0.45f, IM_COL32(150, 150, 150, 255));
        const float strength = std::sqrt(wind.right * wind.right + wind.forward * wind.forward);
        if (strength > 1e-3f)
        {
            const float scale = size * 0.42f / std::max(strength, 1e-3f) * std::min(1.0f, 0.2f + strength / 20.0f);
            const ImVec2 tip(centre.x + wind.right * scale, centre.y - wind.forward * scale);
            draw->AddLine(centre, tip, IM_COL32(80, 200, 255, 255), 2);
            draw->AddCircleFilled(tip, 3, IM_COL32(80, 200, 255, 255));
        }
        draw->AddText(ImVec2(centre.x + wind.north_right * size * 0.4f - 4, centre.y - wind.north_forward * size * 0.4f - 7),
            IM_COL32(220, 220, 220, 255), "N");
        ImGui::Dummy(ImVec2(size, size));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("Now %.1f m/s towards %.0f deg", wind.now_speed, wind.now_heading);
        ImGui::TextDisabled("(+x is 0, +z is 90; gusts %s)", (snapshot.layer_mask >> particle_layout::gusts_mask_bit) & 1 ? "on" : "off");
        bool arrow = views.wind_arrow;
        if (ImGui::Checkbox("Wind arrow (F9)##compass", &arrow)) commands.push_back({UiCommand::Kind::toggle_debug_view, 3});
        ImGui::EndGroup();
    }

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::RadioButton("Fly", !camera.walking) && camera.walking) commands.push_back({UiCommand::Kind::toggle_walk});
        ImGui::SameLine();
        if (ImGui::RadioButton("Walk (G)", camera.walking) && !camera.walking) commands.push_back({UiCommand::Kind::toggle_walk});
        ImGui::SameLine();
        float speed = camera.speed;
        ImGui::SetNextItemWidth(150);
        if (ImGui::SliderFloat("Speed (wheel)", &speed, 1, camera.walking ? 20.0f : 400.0f, "%.0f m/s",
                ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic))
        {
            commands.push_back({UiCommand::Kind::set_camera_speed, 0, {}, speed});
        }
        ImGui::Text("Position %.1f, %.1f, %.1f m; heading %.0f, elevation %.0f deg", camera.position[0], camera.position[1],
            camera.position[2], camera.yaw, camera.pitch);
        for (std::uint32_t index = 0; index < camera.bookmarks.size(); ++index)
        {
            ImGui::PushID(static_cast<int>(index));
            const auto go = "Go F" + std::to_string(index < 6 ? index + 2 : 11);
            if (ImGui::Button(go.c_str())) commands.push_back({UiCommand::Kind::recall_bookmark, index});
            ImGui::SameLine();
            if (ImGui::Button("Store (Ctrl)")) commands.push_back({UiCommand::Kind::store_bookmark, index});
            ImGui::SameLine();
            if (camera.stored[index]) ImGui::TextUnformatted("stored");
            else ImGui::TextDisabled("%.*s", length(camera.bookmarks[index]), camera.bookmarks[index].data());
            ImGui::PopID();
        }
    }

    ImGui::SeparatorText("Debug views");
    const std::pair<const char*, bool> toggles[] = {{"Effect bounds", views.effect_bounds}, {"Collision boxes", views.collision_boxes},
        {"Spawn regions (F8)", views.spawn_regions}, {"Wind arrow (F9)", views.wind_arrow}};
    for (std::uint32_t index = 0; index < std::size(toggles); ++index)
    {
        bool shown = toggles[index].second;
        if (index % 2 != 0) ImGui::SameLine();
        if (ImGui::Checkbox(toggles[index].first, &shown)) commands.push_back({UiCommand::Kind::toggle_debug_view, index});
    }
    ImGui::TextDisabled("L toggles bounds and boxes together");

    if (ImGui::CollapsingHeader("Scalability"))
    {
        const bool automatic = snapshot.load.smoke_resolution == "auto";
        bool coverage = snapshot.load.coverage_flag || automatic;
        if (automatic) ImGui::BeginDisabled();
        if (ImGui::Checkbox("Measure coverage", &coverage)) commands.push_back({UiCommand::Kind::toggle_coverage});
        if (automatic) ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled(automatic ? "(auto measures)" : "(Load rows; scatter cost)");
        if (snapshot.load.realism)
        {
            bool graduate = snapshot.load.slot_policy == "graduate-oldest";
            if (ImGui::Checkbox("Graduate oldest when full", &graduate)) commands.push_back({UiCommand::Kind::set_slot_policy, graduate ? 2u : 0u});
        }
        else
        {
            bool evict = snapshot.load.slot_policy == "evict-oldest";
            if (ImGui::Checkbox("Evict oldest when full", &evict)) commands.push_back({UiCommand::Kind::set_slot_policy, evict ? 1u : 0u});
        }
        constexpr const char* modes[] = {"Full", "Half", "Auto"};
        ImGui::TextUnformatted("Smoke");
        for (std::uint32_t mode = 0; mode < 3; ++mode)
        {
            ImGui::SameLine();
            const bool selected = snapshot.load.smoke_resolution == std::string_view(mode == 0 ? "full" : mode == 1 ? "half" : "auto");
            if (ImGui::RadioButton(modes[mode], selected) && !selected) commands.push_back({UiCommand::Kind::set_smoke_resolution, mode});
        }
        if (snapshot.load.smoke_resolution != "full")
        {
            bool half_light = snapshot.load.half_light;
            if (ImGui::Checkbox("Light at half resolution too", &half_light)) commands.push_back({UiCommand::Kind::toggle_half_light});
        }
        if (snapshot.load.smoke_resolution == "auto" && !snapshot.load.smoke_count_budget)
        {
            if (!budget_editing) budget_edit = static_cast<float>(snapshot.load.smoke_limit);
            ImGui::SetNextItemWidth(160);
            ImGui::SliderFloat("Budget (screens)", &budget_edit, 50, 3000, "%.0f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
            budget_editing = ImGui::IsItemActive();
            if (ImGui::IsItemDeactivatedAfterEdit()) commands.push_back({UiCommand::Kind::set_smoke_budget, 0, {}, budget_edit});
        }
    }

    if (ImGui::CollapsingHeader("Smoke level of detail"))
    {
        constexpr const char* orders[] = {"origin", "none", "cloud"};
        ImGui::TextUnformatted("Order by");
        for (std::uint32_t order = 0; order < 3; ++order)
        {
            ImGui::SameLine();
            const bool selected = snapshot.load.effect_order == orders[order];
            if (ImGui::RadioButton(orders[order], selected) && !selected) commands.push_back({UiCommand::Kind::set_effect_order, order});
        }
        float lod = snapshot.load.smoke_lod;
        ImGui::SetNextItemWidth(160);
        if (ImGui::SliderFloat("Thin above (px)", &lod, 0, 256, lod > 0 ? "%.0f" : "off"))
        {
            commands.push_back({UiCommand::Kind::set_smoke_lod, 0, {}, lod});
        }
        float floor = snapshot.load.faint_floor;
        ImGui::SetNextItemWidth(160);
        if (ImGui::SliderFloat("Faint floor", &floor, 0, 0.01f, floor > 0 ? "%.5f" : "off", ImGuiSliderFlags_Logarithmic))
        {
            commands.push_back({UiCommand::Kind::set_faint_floor, 0, {}, floor});
        }
    }

    if (ImGui::CollapsingHeader("Realism"))
    {
        bool realism = realism_enabled(snapshot.layer_mask);
        if (ImGui::Checkbox("Realism model (clears)", &realism)) commands.push_back({UiCommand::Kind::toggle_layer, realism_layout::realism_mask_bit});
        if (realism_enabled(snapshot.layer_mask))
        {
            bool linger = ((snapshot.layer_mask >> realism_layout::linger_mask_bit) & 1) != 0;
            ImGui::SameLine();
            if (ImGui::Checkbox("linger (new effects)", &linger)) commands.push_back({UiCommand::Kind::toggle_layer, realism_layout::linger_mask_bit});
        }
    }

    if (ImGui::CollapsingHeader("Layers and features"))
    {
        constexpr std::pair<const char*, std::uint32_t> layers[] = {{"dust", 0}, {"smoke", 1}, {"plume", 2}, {"flash", 3},
            {"fireball", 4}, {"sparks", 5}, {"debris", 6}, {"lights", particle_layout::lights_mask_bit},
            {"curl", particle_layout::curl_mask_bit}, {"vortex", particle_layout::vortex_mask_bit}, {"gusts", particle_layout::gusts_mask_bit},
            {"shading", particle_layout::shading_mask_bit}, {"shadow", particle_layout::shadow_mask_bit},
            {"emission", particle_layout::emission_mask_bit}, {"props", particle_layout::props_mask_bit},
            {"collision", particle_layout::collision_mask_bit}, {"deflection", particle_layout::deflection_mask_bit}};
        for (std::size_t index = 0; index < std::size(layers); ++index)
        {
            const auto [name, mask_bit] = layers[index];
            bool shown = ((snapshot.layer_mask >> mask_bit) & 1) != 0;
            if (index % 4 != 0) ImGui::SameLine();
            if (ImGui::Checkbox(name, &shown)) commands.push_back({UiCommand::Kind::toggle_layer, mask_bit});
        }
    }
    ImGui::End();
}

void crosshair(const DiagnosticsSnapshot& snapshot)
{
    const auto& io = ImGui::GetIO();
    auto* draw = ImGui::GetBackgroundDrawList();
    const ImVec2 centre(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    const auto colour = snapshot.showcase.aim ? IM_COL32(255, 255, 255, 220) : IM_COL32(255, 80, 80, 220);
    draw->AddLine(ImVec2(centre.x - 9, centre.y), ImVec2(centre.x - 3, centre.y), colour, 2);
    draw->AddLine(ImVec2(centre.x + 3, centre.y), ImVec2(centre.x + 9, centre.y), colour, 2);
    draw->AddLine(ImVec2(centre.x, centre.y - 9), ImVec2(centre.x, centre.y - 3), colour, 2);
    draw->AddLine(ImVec2(centre.x, centre.y + 3), ImVec2(centre.x, centre.y + 9), colour, 2);
    char label[160];
    const auto preset = preset_name(snapshot.showcase.preset);
    if (snapshot.showcase.aim)
    {
        const auto& aim = *snapshot.showcase.aim;
        const auto where = aim.prop.empty() ? aim.surface : aim.prop;
        std::snprintf(label, sizeof(label), "%.*s x%u | %.*s | %.0f m", length(preset), preset.data(), snapshot.showcase.count,
            length(where), where.data(), aim.distance);
    }
    else std::snprintf(label, sizeof(label), "%.*s x%u | nothing to aim at", length(preset), preset.data(), snapshot.showcase.count);
    const auto size = ImGui::CalcTextSize(label);
    draw->AddText(ImVec2(centre.x - size.x * 0.5f, centre.y + 14), colour, label);
}
}

ImDrawData* DebugUi::build(const DiagnosticsSnapshot& snapshot, std::vector<UiCommand>& commands)
{
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (visible_)
    {
        crosshair(snapshot);
        engine_window(snapshot);
        showcase_window(snapshot, commands, radius_edit_, radius_editing_, budget_edit_, budget_editing_);
    }
    ImGui::Render();
    return visible_ ? ImGui::GetDrawData() : nullptr;
}
}
