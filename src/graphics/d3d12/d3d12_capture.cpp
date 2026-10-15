#include "d3d12_renderer.hpp"

#include <SDL3/SDL.h>

#include <cstring>

namespace pyroclast
{
bool D3D12Renderer::prepare_capture(ID3D12Resource* backbuffer)
{
    const auto description = backbuffer->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 size = 0;
    device_->GetCopyableFootprints(&description, 0, 1, 0, &footprint, nullptr, nullptr, &size);
    if (!capture_readback_ || capture_size_ != size)
    {
        if (capture_readback_ && !wait_idle())
        {
            return false;
        }
        capture_readback_.Reset();
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = size;
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (!check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&capture_readback_)), "Create capture readback"))
        {
            return false;
        }
        capture_size_ = size;
    }
    capture_footprint_ = footprint;
    return true;
}

void D3D12Renderer::record_capture(ID3D12Resource* backbuffer)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = backbuffer;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    command_list_->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = capture_readback_.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = capture_footprint_;
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = backbuffer;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    command_list_->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &barrier);
    capture_pending_ = true;
}

std::optional<CapturedImage> D3D12Renderer::read_capture()
{
    if (!capture_pending_)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No submitted frame requested a capture.");
        return std::nullopt;
    }
    if (!wait_idle())
    {
        return std::nullopt;
    }
    capture_pending_ = false;
    void* mapped = nullptr;
    const D3D12_RANGE read{0, static_cast<SIZE_T>(capture_size_)};
    if (!check(capture_readback_->Map(0, &read, &mapped), "Map capture readback"))
    {
        return std::nullopt;
    }
    CapturedImage image;
    image.width = capture_footprint_.Footprint.Width;
    image.height = capture_footprint_.Footprint.Height;
    image.rgba.resize(static_cast<std::size_t>(image.width) * image.height * 4);
    const auto* rows = static_cast<const std::uint8_t*>(mapped) + capture_footprint_.Offset;
    for (std::uint32_t row = 0; row < image.height; ++row)
    {
        std::memcpy(image.rgba.data() + static_cast<std::size_t>(row) * image.width * 4,
            rows + static_cast<std::size_t>(row) * capture_footprint_.Footprint.RowPitch, image.width * 4);
    }
    const D3D12_RANGE written{0, 0};
    capture_readback_->Unmap(0, &written);
    for (std::size_t pixel = 3; pixel < image.rgba.size(); pixel += 4) image.rgba[pixel] = 255;
    return image;
}
}
