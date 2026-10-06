#include "d3d12_renderer.hpp"
#include "d3d12_resources.hpp"

#include <SDL3/SDL.h>

#include <cassert>
#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace pyroclast
{
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

RenderResult D3D12Renderer::render(const DrawCommand& draw)
{
    assert(swapchain_ && command_list_);
    const auto* vertices = dynamic_cast<const D3D12VertexBuffer*>(&draw.vertices);
    if (!vertices || vertices->owner != this || draw.ranges.empty() ||
        vertices->view.StrideInBytes != particle_layout::particle_stride)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Draw requires an owned particle buffer and at least one range.");
        return RenderResult::failed;
    }
    for (const auto& range : draw.ranges)
    {
        const auto* pipeline = dynamic_cast<const D3D12GraphicsPipeline*>(&range.pipeline);
        if (!pipeline || pipeline->owner != this ||
            range.record_count == 0 || range.first_record > vertices->count ||
            range.record_count > vertices->count - range.first_record)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid draw range or resource ownership.");
            return RenderResult::failed;
        }
    }
    if (half_smoke_ && (static_cast<const D3D12GraphicsPipeline&>(draw.ranges.front().pipeline).blend != BlendMode::premultiplied_alpha ||
        std::count_if(draw.ranges.begin(), draw.ranges.end(), [](const auto& range) {
            return static_cast<const D3D12GraphicsPipeline&>(range.pipeline).blend == BlendMode::premultiplied_alpha;
        }) != 1))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Half-resolution rendering requires one leading particle smoke range.");
        return RenderResult::failed;
    }
    const auto* compute = draw.compute ? dynamic_cast<const D3D12ComputePipeline*>(&draw.compute->pipeline) : nullptr;
    if (draw.compute && (!compute || compute->owner != this || compute->vertices != vertices ||
        draw.compute->group_count == 0 || draw.compute->group_count > 65535 ||
        draw.compute->constants[0] != vertices->count))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid compute dispatch or resource ownership.");
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
    const UINT index = swapchain_->GetCurrentBackBufferIndex();
    auto& frame = frames_[index];

    if (!wait_for_fence(frame.fence_value) || !read_timestamps(index) ||
        !check(frame.allocator->Reset(), "Reset frame command allocator") ||
        !check(command_list_->Reset(frame.allocator.Get(), nullptr), "Reset command list"))
    {
        return RenderResult::failed;
    }

    const UINT query = index * 4;
    if (timestamps_) command_list_->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query);
    if (compute)
    {
        D3D12_RESOURCE_BARRIER storage{};
        storage.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        storage.Transition.pResource = vertices->resource.Get();
        storage.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        storage.Transition.StateBefore = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        storage.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        command_list_->ResourceBarrier(1, &storage);
        command_list_->SetPipelineState(compute->state.Get());
        command_list_->SetComputeRootSignature(compute->root_signature.Get());
        command_list_->SetComputeRootUnorderedAccessView(0, vertices->resource->GetGPUVirtualAddress());
        command_list_->SetComputeRoot32BitConstants(1, 4, draw.compute->constants.data(), 0);
        command_list_->Dispatch(draw.compute->group_count, 1, 1);
        std::swap(storage.Transition.StateBefore, storage.Transition.StateAfter);
        command_list_->ResourceBarrier(1, &storage);
    }

    if (timestamps_)
    {
        command_list_->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query + 1);
        command_list_->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query + 2);
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = frame.backbuffer.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    command_list_->ResourceBarrier(1, &barrier);

    auto rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(index) * rtv_stride_;
    command_list_->ClearRenderTargetView(rtv, draw.clear_color.data(), 0, nullptr);
    set_target(rtv, width_, height_);
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for (const auto& range : draw.ranges)
    {
        const auto& pipeline = static_cast<const D3D12GraphicsPipeline&>(range.pipeline);
        if (half_smoke_ && pipeline.blend == BlendMode::premultiplied_alpha)
            draw_smoke(range, *vertices, draw, rtv);
        else draw_range(range, *vertices, draw.aspect_ratio);
    }

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    command_list_->ResourceBarrier(1, &barrier);
    if (timestamps_)
    {
        command_list_->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query + 3);
        command_list_->ResolveQueryData(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query, 4,
            timestamp_readback_.Get(), query * sizeof(std::uint64_t));
    }
    if (!check(command_list_->Close(), "Close frame command list"))
    {
        return RenderResult::failed;
    }
    ID3D12CommandList* lists[] = {command_list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    const HRESULT present_result = swapchain_->Present(vsync_ ? 1 : 0, 0);
    occluded_ = present_result == DXGI_STATUS_OCCLUDED;
    const auto value = next_fence_value_++;
    if (!check(queue_->Signal(fence_.Get(), value), "Signal frame fence"))
    {
        return RenderResult::failed;
    }
    frame.fence_value = value;
    frame.timestamps_pending = timestamps_ != nullptr;
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
        frame.backbuffer.Reset();
        frame.fence_value = 0;
        frame.timestamps_pending = false;
    }
    if (!check(swapchain_->ResizeBuffers(frame_count, static_cast<UINT>(width),
        static_cast<UINT>(height), backbuffer_format, 0), "Resize swapchain"))
    {
        return false;
    }
    width_ = width;
    height_ = height;
    if (half_smoke_ && !create_smoke_target()) return false;
    if (!create_render_targets())
    {
        return false;
    }
    SDL_Log("D3D12 swapchain resized: %dx%d", width_, height_);
    return true;
}

D3D12Renderer::~D3D12Renderer()
{
    if (queue_ && fence_ && fence_event_.IsValid())
    {
        wait_idle();
    }
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
}
