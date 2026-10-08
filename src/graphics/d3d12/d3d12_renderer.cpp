#include "d3d12_renderer.hpp"
#include "d3d12_resources.hpp"
#include "../draw_validation.hpp"

#include <SDL3/SDL.h>

#include <cassert>
#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace pyroclast
{
namespace
{
// Shader reads from any stage; a buffer read by both compute and graphics needs no transition between them.
constexpr auto buffer_read_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

ID3D12Resource* native(const Buffer* buffer)
{
    return buffer ? static_cast<const D3D12Buffer*>(buffer)->resource.Get() : nullptr;
}

// Metadata 1 marks an ANSI string, which PIX reads without WinPixEventRuntime.
void begin_event(ID3D12GraphicsCommandList* commands, const char* label)
{
    if (label) commands->BeginEvent(1, label, static_cast<UINT>(std::strlen(label) + 1));
}

void end_event(ID3D12GraphicsCommandList* commands, const char* label)
{
    if (label) commands->EndEvent();
}
}

bool D3D12Renderer::check(HRESULT result, const char* operation)
{
    if (SUCCEEDED(result))
    {
        return true;
    }
    char description[512]{};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(result), 0, description,
        static_cast<DWORD>(sizeof(description)), nullptr);
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s failed (0x%08lX): %s",
        operation, static_cast<unsigned long>(result), description);
    if (device_ && (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Device removal reason: 0x%08lX",
            static_cast<unsigned long>(device_->GetDeviceRemovedReason()));
    }
    return false;
}

bool D3D12Renderer::check_validation()
{
    if (!info_queue_)
    {
        return true;
    }
    const auto count = info_queue_->GetNumStoredMessagesAllowedByRetrievalFilter();
    for (std::uint64_t index = 0; index < count; ++index)
    {
        SIZE_T size = 0;
        if (!check(info_queue_->GetMessage(index, nullptr, &size), "Query validation message size"))
        {
            return false;
        }
        std::vector<char> storage(size);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        if (!check(info_queue_->GetMessage(index, message, &size), "Read validation message"))
        {
            return false;
        }
        if (message->Severity == D3D12_MESSAGE_SEVERITY_WARNING)
        {
            ++validation_warnings_;
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "D3D12 validation: %s", message->pDescription);
        }
        else if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
            message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)
        {
            ++validation_errors_;
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "D3D12 validation: %s", message->pDescription);
        }
    }
    info_queue_->ClearStoredMessages();
    return validation_errors_ == 0;
}

bool D3D12Renderer::wait_for_fence(std::uint64_t value)
{
    auto completed = fence_->GetCompletedValue();
    if (completed == std::numeric_limits<std::uint64_t>::max())
    {
        return check(DXGI_ERROR_DEVICE_REMOVED, "Wait for GPU fence");
    }
    if (completed >= value)
    {
        return true;
    }
    if (!check(fence_->SetEventOnCompletion(value, fence_event_.Get()), "Arm GPU fence event"))
    {
        return false;
    }
    const DWORD wait_result = WaitForSingleObject(fence_event_.Get(), INFINITE);
    if (wait_result != WAIT_OBJECT_0)
    {
        return check(HRESULT_FROM_WIN32(GetLastError()), "Wait for GPU fence event");
    }
    completed = fence_->GetCompletedValue();
    if (completed == std::numeric_limits<std::uint64_t>::max())
    {
        return check(DXGI_ERROR_DEVICE_REMOVED, "Complete GPU fence wait");
    }
    assert(completed >= value);
    return true;
}

bool D3D12Renderer::wait_idle()
{
    const auto value = next_fence_value_++;
    return check(queue_->Signal(fence_.Get(), value), "Signal GPU idle fence") &&
        wait_for_fence(value) && check_validation();
}

void D3D12Renderer::set_target(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const D3D12_CPU_DESCRIPTOR_HANDLE* dsv, int width, int height)
{
    command_list_->OMSetRenderTargets(1, &rtv, FALSE, dsv);
    const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    const D3D12_RECT scissor{0, 0, width, height};
    command_list_->RSSetViewports(1, &viewport);
    command_list_->RSSetScissorRects(1, &scissor);
}

void D3D12Renderer::bind(const Bindings& bindings, bool graphics)
{
    for (UINT slot = 0; slot < bindings.read.size(); ++slot)
    {
        if (!bindings.read[slot]) continue;
        const auto address = native(bindings.read[slot])->GetGPUVirtualAddress();
        if (graphics) command_list_->SetGraphicsRootShaderResourceView(root_read_buffers + slot, address);
        else command_list_->SetComputeRootShaderResourceView(root_read_buffers + slot, address);
    }
    for (UINT slot = 0; slot < bindings.write.size(); ++slot)
    {
        if (bindings.write[slot])
        {
            command_list_->SetComputeRootUnorderedAccessView(root_write_buffers + slot, native(bindings.write[slot])->GetGPUVirtualAddress());
        }
    }
}

void D3D12Renderer::draw(const Draw& draw)
{
    begin_event(command_list_.Get(), draw.label);
    const auto& pipeline = static_cast<const D3D12GraphicsPipeline&>(draw.pipeline);
    command_list_->SetPipelineState(pipeline.state.Get());
    command_list_->IASetPrimitiveTopology(pipeline.topology);
    command_list_->SetGraphicsRoot32BitConstants(root_draw_constants, binding_layout::draw_constant_words, draw.constants.data(), 0);
    bind(draw.bindings, true);
    if (draw.indirect)
    {
        command_list_->ExecuteIndirect(draw_signature_.Get(), 1, native(draw.indirect), draw.indirect_offset, nullptr, 0);
    }
    else
    {
        command_list_->DrawInstanced(draw.vertex_count, draw.instance_count, 0, 0);
    }
    end_event(command_list_.Get(), draw.label);
}

bool D3D12Renderer::owns(const Frame& frame) const
{
    const auto buffer_owned = [this](const Buffer* buffer) {
        const auto* resource = dynamic_cast<const D3D12Buffer*>(buffer);
        return buffer == nullptr || (resource && resource->owner == this);
    };
    const auto buffers_owned = [&buffer_owned](const Bindings& bindings) {
        return std::all_of(bindings.read.begin(), bindings.read.end(), buffer_owned) &&
            std::all_of(bindings.write.begin(), bindings.write.end(), buffer_owned);
    };
    const auto target_owned = [this](const RenderTarget* target) {
        const auto* native_target = dynamic_cast<const D3D12RenderTarget*>(target);
        return target == nullptr || (native_target && native_target->owner == this);
    };
    if (!buffer_owned(frame.readback.source)) return false;
    for (const auto& dispatch : frame.dispatches)
    {
        const auto* pipeline = dynamic_cast<const D3D12ComputePipeline*>(&dispatch.pipeline);
        if (!pipeline || pipeline->owner != this || !buffers_owned(dispatch.bindings)) return false;
    }
    for (const auto& pass : frame.passes)
    {
        if (!target_owned(pass.target) || !target_owned(pass.depth) ||
            !std::all_of(pass.inputs.begin(), pass.inputs.end(), target_owned)) return false;
        for (const auto& draw : pass.draws)
        {
            const auto* pipeline = dynamic_cast<const D3D12GraphicsPipeline*>(&draw.pipeline);
            if (!pipeline || pipeline->owner != this || !buffers_owned(draw.bindings) || !buffer_owned(draw.indirect)) return false;
        }
    }
    return true;
}

RenderResult D3D12Renderer::render(const Frame& frame)
{
    assert(swapchain_ && command_list_);
    const char* error = !owns(frame) ? "Frame uses resources from another graphics device." :
        frame.capture && !capture_enabled_ ? "Frame capture requires a device created with capture enabled." :
        frame.overlay && !overlay_ ? "Frame overlay requires a device created with the debug overlay." :
        validate_frame(frame);
    if (error)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error);
        return RenderResult::failed;
    }
    if (occluded_)
    {
        SDL_Delay(50);
        const HRESULT result = swapchain_->Present(0, DXGI_PRESENT_TEST);
        if (result == DXGI_STATUS_OCCLUDED)
        {
            return RenderResult::skipped;
        }
        if (!check(result, "Test swapchain visibility"))
        {
            return RenderResult::failed;
        }
        occluded_ = false;
    }
    for (const auto& pass : frame.passes)
    {
        if ((pass.target && !size_target(static_cast<D3D12RenderTarget&>(*pass.target))) ||
            (pass.depth && !size_target(static_cast<D3D12RenderTarget&>(*pass.depth))))
        {
            return RenderResult::failed;
        }
    }
    const UINT backbuffer_index = swapchain_->GetCurrentBackBufferIndex();
    auto* backbuffer = backbuffers_[backbuffer_index].Get();
    auto& slot = frames_[frame_index_];

    const auto wait_begin = SDL_GetPerformanceCounter();
    const bool slot_free = wait_for_fence(slot.fence_value);
    const auto wait_end = SDL_GetPerformanceCounter();
    if (!slot_free || !read_timestamps(frame_index_) ||
        !check(slot.allocator->Reset(), "Reset frame command allocator") ||
        !check(command_list_->Reset(slot.allocator.Get(), nullptr), "Reset command list"))
    {
        return RenderResult::failed;
    }
    read_readback(slot);
    std::memcpy(slot.mapped_constants, frame.constants.data(), frame.constants.size());
    const auto frame_constants = slot.constants->GetGPUVirtualAddress();
    ID3D12DescriptorHeap* heaps[] = {shader_heap_.Get()};
    command_list_->SetDescriptorHeaps(1, heaps);
    buffer_states_.reset();

    slot.region_count = 0;
    const auto query = static_cast<UINT>(frame_index_) * queries_per_slot;
    if (timestamps_) command_list_->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query);
    if (!frame.dispatches.empty())
    {
        command_list_->SetComputeRootSignature(root_signature_.Get());
        command_list_->SetComputeRootConstantBufferView(root_frame_constants, frame_constants);
    }
    for (const auto& dispatch : frame.dispatches)
    {
        const auto region = begin_region(dispatch.label, "Dispatch");
        for (auto* buffer : dispatch.bindings.write) if (buffer) buffer_states_.require(native(buffer), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        for (const auto* buffer : dispatch.bindings.read) if (buffer) buffer_states_.require(native(buffer), buffer_read_state);
        buffer_states_.flush(command_list_.Get());
        begin_event(command_list_.Get(), dispatch.label);
        command_list_->SetPipelineState(static_cast<const D3D12ComputePipeline&>(dispatch.pipeline).state.Get());
        command_list_->SetComputeRoot32BitConstants(root_draw_constants, binding_layout::draw_constant_words, dispatch.constants.data(), 0);
        bind(dispatch.bindings, false);
        command_list_->Dispatch(dispatch.groups[0], dispatch.groups[1], dispatch.groups[2]);
        end_event(command_list_.Get(), dispatch.label);
        end_region(region);
    }
    for (const auto& pass : frame.passes)
    {
        for (const auto& draw : pass.draws)
        {
            for (const auto* buffer : draw.bindings.read) if (buffer) buffer_states_.require(native(buffer), buffer_read_state);
            if (draw.indirect) buffer_states_.require(native(draw.indirect), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        }
    }
    if (frame.readback.source) buffer_states_.require(native(frame.readback.source), D3D12_RESOURCE_STATE_COPY_SOURCE);
    buffer_states_.flush(command_list_.Get());

    if (timestamps_) command_list_->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query + 1);
    if (frame.readback.source)
    {
        command_list_->CopyBufferRegion(slot.readback.Get(), 0, native(frame.readback.source), frame.readback.offset, frame.readback.size);
        slot.readback_size = frame.readback.size;
    }
    if (timestamps_) command_list_->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query + 2);
    auto backbuffer_view = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    backbuffer_view.ptr += static_cast<SIZE_T>(backbuffer_index) * rtv_stride_;
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    command_list_->SetGraphicsRootConstantBufferView(root_frame_constants, frame_constants);
    for (const auto& pass : frame.passes)
    {
        const auto* target = static_cast<const D3D12RenderTarget*>(pass.target);
        const auto* depth = static_cast<const D3D12RenderTarget*>(pass.depth);
        const auto region = begin_region(pass.label, "Pass");
        begin_event(command_list_.Get(), pass.label);
        std::array<D3D12_RESOURCE_BARRIER, 2> barriers{};
        UINT barrier_count = 0;
        const auto transition = [&barriers, &barrier_count](ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
            auto& barrier = barriers[barrier_count++];
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = from;
            barrier.Transition.StateAfter = to;
        };
        transition(target ? target->texture.Get() : backbuffer, target ? color_resting_state : D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        if (depth && pass.depth_write) transition(depth->texture.Get(), depth_resting_state, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        command_list_->ResourceBarrier(barrier_count, barriers.data());

        const auto view = target ? target->render_view->GetCPUDescriptorHandleForHeapStart() : backbuffer_view;
        D3D12_CPU_DESCRIPTOR_HANDLE depth_view{};
        if (depth)
        {
            depth_view = depth->render_view->GetCPUDescriptorHandleForHeapStart();
            if (!pass.depth_write) depth_view.ptr += dsv_stride_;
        }
        set_target(view, depth ? &depth_view : nullptr, target ? target->width : width_, target ? target->height : height_);
        const auto& clear_color = target ? target->description().clear_color : frame.clear_color;
        if (!pass.load) command_list_->ClearRenderTargetView(view, clear_color.data(), 0, nullptr);
        if (depth && pass.depth_write)
        {
            command_list_->ClearDepthStencilView(depth_view, D3D12_CLEAR_FLAG_DEPTH, depth->description().clear_depth, 0, 0, nullptr);
        }
        for (UINT slot_index = 0; slot_index < pass.inputs.size(); ++slot_index)
        {
            const auto* input = static_cast<const D3D12RenderTarget*>(pass.inputs[slot_index]);
            command_list_->SetGraphicsRootDescriptorTable(root_textures + slot_index, shader_descriptor(input ? input->shader_view : 0));
        }
        for (const auto& item : pass.draws) draw(item);
        end_region(region);
        if (!target && frame.overlay)
        {
            if (timestamps_) command_list_->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query + 3);
            const auto overlay_region = begin_region("Overlay", "Overlay");
            begin_event(command_list_.Get(), "Overlay");
            record_overlay(frame.overlay, view);
            end_event(command_list_.Get(), "Overlay");
            end_region(overlay_region);
        }

        for (UINT index = 0; index < barrier_count; ++index)
        {
            std::swap(barriers[index].Transition.StateBefore, barriers[index].Transition.StateAfter);
        }
        command_list_->ResourceBarrier(barrier_count, barriers.data());
        end_event(command_list_.Get(), pass.label);
    }
    if (timestamps_)
    {
        if (!frame.overlay) command_list_->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query + 3);
        command_list_->ResolveQueryData(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query, 4 + 2 * slot.region_count,
            timestamp_readback_.Get(), query * sizeof(std::uint64_t));
    }
    if (frame.capture && !record_capture(backbuffer))
    {
        return RenderResult::failed;
    }
    if (!check(command_list_->Close(), "Close frame command list"))
    {
        return RenderResult::failed;
    }
    ID3D12CommandList* lists[] = {command_list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    const auto present_begin = SDL_GetPerformanceCounter();
    const HRESULT present_result = swapchain_->Present(vsync_ ? 1 : 0, tearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0);
    const auto present_end = SDL_GetPerformanceCounter();
    const double milliseconds = 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
    submit_timings_ = {static_cast<double>(wait_end - wait_begin) * milliseconds, static_cast<double>(present_end - present_begin) * milliseconds};
    occluded_ = present_result == DXGI_STATUS_OCCLUDED;
    const auto value = next_fence_value_++;
    if (!check(queue_->Signal(fence_.Get(), value), "Signal frame fence"))
    {
        return RenderResult::failed;
    }
    slot.fence_value = value;
    slot.timestamps_pending = timestamps_ != nullptr;
    slot.readback_pending = frame.readback.source != nullptr;
    frame_index_ = (frame_index_ + 1) % frames_.size();
    return check(present_result, "Present frame") && check_validation() ? RenderResult::submitted : RenderResult::failed;
}

bool D3D12Renderer::resize(int width, int height)
{
    assert(swapchain_);
    if (width <= 0 || height <= 0 || (width == width_ && height == height_))
    {
        return true;
    }

    if (!wait_idle())
    {
        return false;
    }
    for (auto& frame : frames_)
    {
        frame.fence_value = 0;
        frame.timestamps_pending = false;
    }
    for (auto& buffer : backbuffers_) buffer.Reset();
    if (!check(swapchain_->ResizeBuffers(backbuffer_count, static_cast<UINT>(width),
        static_cast<UINT>(height), backbuffer_format, tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0), "Resize swapchain"))
    {
        return false;
    }
    width_ = width;
    height_ = height;
    if (!create_render_targets())
    {
        return false;
    }
    ++swapchain_rebuilds_;
    SDL_Log("D3D12 swapchain resized: %dx%d", width_, height_);
    return true;
}

D3D12Renderer::~D3D12Renderer()
{
    if (queue_ && fence_ && fence_event_.IsValid())
    {
        wait_idle();
    }
    destroy_overlay();
    if (mapped_timestamps_)
    {
        const D3D12_RANGE written{0, 0};
        timestamp_readback_->Unmap(0, &written);
    }
    check_validation();
    if (info_queue_)
    {
        SDL_Log("D3D12 validation summary: %llu warnings, %llu errors",
            static_cast<unsigned long long>(validation_warnings_),
            static_cast<unsigned long long>(validation_errors_));
    }
}

std::optional<GpuMemory> D3D12Renderer::memory_usage() const
{
    if (!adapter_) return std::nullopt;
    DXGI_QUERY_VIDEO_MEMORY_INFO local{};
    DXGI_QUERY_VIDEO_MEMORY_INFO non_local{};
    if (FAILED(adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local)) ||
        FAILED(adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &non_local)))
    {
        return std::nullopt;
    }
    return GpuMemory{local.CurrentUsage, local.Budget, non_local.CurrentUsage, non_local.Budget};
}
}
