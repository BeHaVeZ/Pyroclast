#include "debug_ui.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <numeric>

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
            ImGui::TextDisabled("GPU timestamps off (start with --gpu-timing)");
        }

        ImGui::SeparatorText("Simulation");
        if (ImGui::BeginTable("simulation", 2, ImGuiTableFlags_SizingStretchProp))
        {
            row("Explosions", "%u", snapshot.explosions);
            row("Pool capacity", "%u particles", snapshot.capacity);
            row("Smoke resolution", "%s", snapshot.half_resolution_smoke ? "half" : "full");
            row("State", "%s", snapshot.paused ? "paused" : "running");
            ImGui::EndTable();
        }
        if (ImGui::Button("Trigger (Space)")) commands.push_back(UiCommand::trigger);
        ImGui::SameLine();
        if (ImGui::Button("Clear (R)")) commands.push_back(UiCommand::clear);
        ImGui::SameLine();
        if (ImGui::Button(snapshot.paused ? "Resume (P)" : "Pause (P)")) commands.push_back(UiCommand::toggle_pause);
        ImGui::SameLine();
        ImGui::BeginDisabled(!snapshot.paused);
        if (ImGui::Button("Step (N)")) commands.push_back(UiCommand::step);
        ImGui::EndDisabled();
        ImGui::End();
    }
    ImGui::Render();
    return visible_ ? ImGui::GetDrawData() : nullptr;
}
}
