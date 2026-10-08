#include "debug_ui.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
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
}

ImDrawData* DebugUi::build(const DiagnosticsSnapshot& snapshot, std::vector<UiCommand>& commands)
{
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (visible_)
    {
        ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
        ImGui::Begin("Pyroclast (F1 hides)", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        if (ImGui::BeginTable("system", 2, ImGuiTableFlags_SizingStretchProp))
        {
            row("Renderer", "%.*s (%.*s)", static_cast<int>(snapshot.renderer.size()), snapshot.renderer.data(),
                static_cast<int>(snapshot.build.size()), snapshot.build.data());
            row("GPU", "%.*s", static_cast<int>(snapshot.adapter.size()), snapshot.adapter.data());
            row("Resolution", "%d x %d, VSync %s", snapshot.width, snapshot.height, snapshot.vsync ? "on" : "off");
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Frame");
        const auto& frames = snapshot.frame_ms;
        if (!frames.empty())
        {
            const float average = std::accumulate(frames.begin(), frames.end(), 0.0f) / static_cast<float>(frames.size());
            const auto [low, high] = std::minmax_element(frames.begin(), frames.end());
            ImGui::Text("CPU frame interval %.2f ms (min %.2f, max %.2f, last %zu)", average, *low, *high, frames.size());
            ImGui::PlotLines("##frames", frames.data(), static_cast<int>(frames.size()), snapshot.frame_ms_offset,
                nullptr, 0.0f, std::max(*high, 1.0f), ImVec2(380, 48));
        }
        if (snapshot.gpu)
        {
            const auto& gpu = *snapshot.gpu;
            ImGui::Text("GPU compute interval %.3f ms, draw interval %.3f ms", gpu.compute_ms, gpu.draw_ms);
            if (gpu.region_count != 0 && ImGui::BeginTable("regions", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
            {
                for (std::uint32_t index = 0; index < gpu.region_count; ++index)
                {
                    row(gpu.regions[index].label, "%.3f ms", gpu.regions[index].milliseconds);
                }
                ImGui::EndTable();
            }
        }
        else
        {
            ImGui::TextDisabled("No GPU timestamp sample yet");
        }

        ImGui::SeparatorText("Simulation");
        if (ImGui::BeginTable("simulation", 2, ImGuiTableFlags_SizingStretchProp))
        {
            row("Startup group", "%u (Space)", snapshot.explosions);
            row("Effects", "%u of %u slots, %llu started, %llu dropped", snapshot.active_effects, snapshot.effect_slots,
                static_cast<unsigned long long>(snapshot.effects_started), static_cast<unsigned long long>(snapshot.effects_dropped));
            row("Simulation tick", "%u", snapshot.tick);
            if (snapshot.particles)
            {
                const auto& counts = *snapshot.particles;
                row("Alive particles", "%u (smoke %u, light %u, debris %u) at tick %u", counts.alive, counts.smoke, counts.light,
                    counts.debris, counts.tick);
            }
            else row("Alive particles", "none");
            row("Smoke resolution", "%s", snapshot.half_resolution_smoke ? "half" : "full");
            row("Scene target", "%.*s, exposure %.2f", static_cast<int>(snapshot.scene_format.size()), snapshot.scene_format.data(),
                snapshot.exposure);
            row("State", "%s", snapshot.paused ? "paused" : snapshot.time_scale == 1 ? "running 1x" : "running 0.25x");
            row("Capped frames", "%llu (catch-up clipped at 4 steps)", static_cast<unsigned long long>(snapshot.capped_frames));
            ImGui::EndTable();
        }
        ImGui::SeparatorText("Camera");
        if (ImGui::BeginTable("camera", 2, ImGuiTableFlags_SizingStretchProp))
        {
            row("Position", "%.1f, %.1f, %.1f m", snapshot.camera_position[0], snapshot.camera_position[1], snapshot.camera_position[2]);
            row("Heading, elevation", "%.0f, %.0f deg", snapshot.camera_yaw, snapshot.camera_pitch);
            row("Speed (wheel)", "%.0f m/s", snapshot.camera_speed);
            ImGui::EndTable();
        }
        ImGui::SeparatorText("Explosions");
        for (std::uint32_t index = 0; index < preset_count; ++index)
        {
            const auto preset = static_cast<ExplosionPreset>(index);
            const auto label = std::to_string(index + 1) + " " + std::string(preset_name(preset));
            if (index % 4 != 0) ImGui::SameLine();
            if (ImGui::RadioButton(label.c_str(), preset == snapshot.click_preset)) commands.push_back({UiCommand::Kind::select_preset, index});
        }
        for (const std::uint32_t count : {1u, 10u, 50u, 100u})
        {
            if (count != 1) ImGui::SameLine();
            const auto label = "x" + std::to_string(count);
            if (ImGui::RadioButton(label.c_str(), snapshot.spawn_count == count)) commands.push_back({UiCommand::Kind::set_count, count});
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(C)");
        if (ImGui::Button("Fire (F)")) commands.push_back({UiCommand::Kind::fire, 0});
        for (const std::uint32_t count : {1u, 10u, 50u, 100u})
        {
            ImGui::SameLine();
            const auto label = "Spawn " + std::to_string(count);
            if (ImGui::Button(label.c_str())) commands.push_back({UiCommand::Kind::fire, count});
        }
        if (ImGui::Button("Barrage (B)")) commands.push_back({UiCommand::Kind::barrage});
        ImGui::SameLine();
        if (snapshot.barrage_progress) ImGui::Text("%u of 80 impacts", *snapshot.barrage_progress);
        else ImGui::TextDisabled("80 impacts, 4 a second, across the view");
        if (ImGui::Button(snapshot.bombardment_impacts ? "Stop bombardment (V)" : "Bombardment (V)"))
        {
            commands.push_back({UiCommand::Kind::toggle_bombardment});
        }
        ImGui::SameLine();
        if (snapshot.bombardment_impacts) ImGui::Text("%u impacts", *snapshot.bombardment_impacts);
        else ImGui::TextDisabled("around the crosshair until stopped");
        constexpr std::uint32_t intervals[] = {60, 30, 15, 10, 6, 4, 3, 2, 1};
        int selected_interval = 2;
        for (int index = 0; index < static_cast<int>(std::size(intervals)); ++index)
        {
            if (intervals[index] == snapshot.bombardment_interval) selected_interval = index;
        }
        const auto rate_label = std::to_string(60 / snapshot.bombardment_interval) + " impacts/s";
        ImGui::SetNextItemWidth(140);
        if (ImGui::BeginCombo("Rate", rate_label.c_str()))
        {
            for (int index = 0; index < static_cast<int>(std::size(intervals)); ++index)
            {
                const auto label = std::to_string(60 / intervals[index]) + " impacts/s";
                if (ImGui::Selectable(label.c_str(), index == selected_interval))
                {
                    commands.push_back({UiCommand::Kind::set_bombardment, intervals[index], {}, snapshot.bombardment_radius});
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        float radius = snapshot.bombardment_radius;
        ImGui::SetNextItemWidth(140);
        ImGui::SliderFloat("Radius (m)", &radius, 5, 60, "%.0f", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            commands.push_back({UiCommand::Kind::set_bombardment, snapshot.bombardment_interval, {}, radius});
        }
        if (snapshot.preset_ticks != 0)
        {
            ImGui::TextDisabled("Expected about %u of %u slots at this rate; at most %.1f impacts/s sustained",
                snapshot.preset_ticks / snapshot.bombardment_interval, snapshot.effect_slots,
                static_cast<double>(snapshot.effect_slots) * 60.0 / static_cast<double>(snapshot.preset_ticks));
        }
        if (ImGui::BeginTable("load", 2, ImGuiTableFlags_SizingStretchProp))
        {
            row("Slots", "%u of %u busy, peak %u since clear, %zu queued", snapshot.active_effects, snapshot.effect_slots,
                snapshot.peak_since_clear, snapshot.queued_events);
            row("Dropped", "%llu since clear, %llu in the last 60 ticks", static_cast<unsigned long long>(snapshot.dropped_since_clear),
                static_cast<unsigned long long>(snapshot.dropped_recent));
            ImGui::EndTable();
        }
        constexpr std::pair<const char*, std::uint32_t> toggles[] = {{"dust", 0}, {"smoke", 1}, {"plume", 2}, {"flash", 3},
            {"fireball", 4}, {"sparks", 5}, {"debris", 6}, {"lights", particle_layout::lights_mask_bit},
            {"curl", particle_layout::curl_mask_bit}, {"vortex", particle_layout::vortex_mask_bit}, {"gusts", particle_layout::gusts_mask_bit},
            {"shading", particle_layout::shading_mask_bit}, {"shadow", particle_layout::shadow_mask_bit},
            {"emission", particle_layout::emission_mask_bit}, {"props", particle_layout::props_mask_bit},
            {"collision", particle_layout::collision_mask_bit}, {"deflection", particle_layout::deflection_mask_bit}};
        for (std::size_t index = 0; index < std::size(toggles); ++index)
        {
            const auto [name, mask_bit] = toggles[index];
            bool shown = ((snapshot.layer_mask >> mask_bit) & 1) != 0;
            if (index % 4 != 0) ImGui::SameLine();
            if (ImGui::Checkbox(name, &shown)) commands.push_back({UiCommand::Kind::toggle_layer, mask_bit});
        }
        ImGui::Text("Temporary lights: %u", snapshot.lights);
        auto wind = snapshot.wind;
        constexpr auto clamp = ImGuiSliderFlags_AlwaysClamp;
        bool changed = ImGui::SliderFloat("Wind speed (m/s)", &wind.speed, 0, 20, "%.1f", clamp);
        changed |= ImGui::SliderFloat("Wind heading (deg)", &wind.direction_degrees, -180, 180, "%.0f", clamp);
        changed |= ImGui::SliderFloat("Gustiness", &wind.gustiness, 0, 1, "%.2f", clamp);
        if (changed) commands.push_back({UiCommand::Kind::set_wind, 0, wind});
        if (ImGui::Button("Trigger (Space)")) commands.push_back({UiCommand::Kind::trigger});
        ImGui::SameLine();
        if (ImGui::Button("Clear (R)")) commands.push_back({UiCommand::Kind::clear});
        ImGui::SameLine();
        if (ImGui::Button(snapshot.paused ? "Resume (P)" : "Pause (P)")) commands.push_back({UiCommand::Kind::toggle_pause});
        ImGui::SameLine();
        ImGui::BeginDisabled(!snapshot.paused);
        if (ImGui::Button("Step (N)")) commands.push_back({UiCommand::Kind::step});
        ImGui::EndDisabled();
        ImGui::SameLine();
        bool slow = snapshot.time_scale != 1;
        if (ImGui::Checkbox("Slow motion 0.25x (T)", &slow)) commands.push_back({UiCommand::Kind::toggle_slow_motion});
        ImGui::SeparatorText("Debug views");
        const std::pair<const char*, bool> debug_views[] = {{"Effect bounds", snapshot.effect_bounds}, {"Collision boxes", snapshot.collision_boxes},
            {"Spawn regions (F8)", snapshot.spawn_regions}, {"Wind arrow (F9)", snapshot.wind_arrow}};
        for (std::uint32_t index = 0; index < std::size(debug_views); ++index)
        {
            bool shown = debug_views[index].second;
            if (index % 2 != 0) ImGui::SameLine();
            if (ImGui::Checkbox(debug_views[index].first, &shown)) commands.push_back({UiCommand::Kind::toggle_debug_view, index});
        }
        ImGui::TextDisabled("L toggles bounds and boxes. Left click: explosion at the cursor");
        ImGui::End();
    }
    ImGui::Render();
    return visible_ ? ImGui::GetDrawData() : nullptr;
}
}
