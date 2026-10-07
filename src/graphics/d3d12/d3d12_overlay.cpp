#include "d3d12_renderer.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>

#include <algorithm>

namespace pyroclast
{
bool D3D12Renderer::create_overlay()
{
    for (UINT index = overlay_descriptor_first; index < shader_descriptor_capacity; ++index) overlay_descriptors_.push_back(index);
    ImGui_ImplDX12_InitInfo init;
    init.Device = device_.Get();
    init.CommandQueue = queue_.Get();
    init.NumFramesInFlight = frame_count;
    init.RTVFormat = backbuffer_format;
    init.DSVFormat = DXGI_FORMAT_UNKNOWN;
    init.UserData = this;
    init.SrvDescriptorHeap = shader_heap_.Get();
    init.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
        auto& renderer = *static_cast<D3D12Renderer*>(info->UserData);
        IM_ASSERT(!renderer.overlay_descriptors_.empty() && "Overlay descriptor range exhausted");
        const UINT index = renderer.overlay_descriptors_.back();
        renderer.overlay_descriptors_.pop_back();
        *cpu = renderer.shader_heap_->GetCPUDescriptorHandleForHeapStart();
        cpu->ptr += static_cast<SIZE_T>(index) * renderer.shader_descriptor_stride_;
        *gpu = renderer.shader_descriptor(index);
    };
    init.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE) {
        auto& renderer = *static_cast<D3D12Renderer*>(info->UserData);
        const auto start = renderer.shader_heap_->GetCPUDescriptorHandleForHeapStart().ptr;
        renderer.overlay_descriptors_.push_back(static_cast<UINT>((cpu.ptr - start) / renderer.shader_descriptor_stride_));
    };
    if (!ImGui_ImplDX12_Init(&init))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Dear ImGui D3D12 renderer failed to initialize.");
        return false;
    }
    overlay_ = true;
    return check_validation();
}

void D3D12Renderer::destroy_overlay()
{
    if (!overlay_) return;
    ImGui_ImplDX12_Shutdown();
    overlay_ = false;
}

void D3D12Renderer::record_overlay(ImDrawData* overlay, D3D12_CPU_DESCRIPTOR_HANDLE backbuffer_view)
{
    command_list_->OMSetRenderTargets(1, &backbuffer_view, FALSE, nullptr);
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplDX12_RenderDrawData(overlay, command_list_.Get());
}
}
