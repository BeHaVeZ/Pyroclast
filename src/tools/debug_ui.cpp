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
            row("State", "%s", snapshot.paused ? "paused" : "running");
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
        ImGui::SeparatorText("Effects");
        const auto selected = preset_name(snapshot.click_preset);
        if (ImGui::BeginCombo("Click preset", std::string(selected).c_str()))
        {
            for (std::uint32_t index = 0; index < preset_count; ++index)
            {
                const auto preset = static_cast<ExplosionPreset>(index);
                if (ImGui::Selectable(std::string(preset_name(preset)).c_str(), preset == snapshot.click_preset))
                {
                    commands.push_back({UiCommand::Kind::select_preset, index});
                }
            }
            ImGui::EndCombo();
        }
        constexpr std::pair<const char*, std::uint32_t> toggles[] = {{"dust", 0}, {"smoke", 1}, {"plume", 2}, {"flash", 3},
            {"fireball", 4}, {"sparks", 5}, {"debris", 6}, {"lights", particle_layout::lights_mask_bit},
            {"curl", particle_layout::curl_mask_bit}, {"vortex", particle_layout::vortex_mask_bit}, {"gusts", particle_layout::gusts_mask_bit},
            {"shading", particle_layout::shading_mask_bit}, {"shadow", particle_layout::shadow_mask_bit},
            {"emission", particle_layout::emission_mask_bit}};
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
        bool lines = snapshot.debug_lines;
        if (ImGui::Checkbox("Effect lines (L)", &lines)) commands.push_back({UiCommand::Kind::toggle_debug_lines});
        ImGui::SameLine();
        ImGui::TextDisabled("Left click: explosion on the terrain");
        ImGui::End();
    }
    ImGui::Render();
    return visible_ ? ImGui::GetDrawData() : nullptr;
}
}
