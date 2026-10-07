#include "d3d12_renderer.hpp"
#include <SDL3/SDL.h>

namespace pyroclast
{
bool D3D12Renderer::create_timestamps()
{
    D3D12_QUERY_HEAP_DESC queries{};
    queries.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queries.Count = frame_count * queries_per_slot;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = queries.Count * sizeof(std::uint64_t);
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (!check(queue_->GetTimestampFrequency(&timestamp_frequency_), "Query timestamp frequency")) return false;
    if (timestamp_frequency_ == 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "D3D12 queue reported a zero timestamp frequency.");
        return false;
    }
    if (!check(device_->CreateQueryHeap(&queries, IID_PPV_ARGS(&timestamps_)), "Create timestamp heap") ||
        !check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&timestamp_readback_)), "Create timestamp readback"))
    {
        return false;
    }
    void* mapped = nullptr;
    const D3D12_RANGE range{0, static_cast<SIZE_T>(buffer.Width)};
    if (!check(timestamp_readback_->Map(0, &range, &mapped), "Map timestamp readback")) return false;
    mapped_timestamps_ = static_cast<const std::uint64_t*>(mapped);
    SDL_Log("GPU timestamp measurements enabled (frame-fence protected results).");
    return true;
}

bool D3D12Renderer::read_timestamps(std::size_t frame)
{
    auto& slot = frames_[frame];
    if (!slot.timestamps_pending) return true;
    const auto* values = mapped_timestamps_ + frame * queries_per_slot;
    const double scale = 1000.0 / static_cast<double>(timestamp_frequency_);
    GpuTimings timings{static_cast<double>(values[1] - values[0]) * scale,
        static_cast<double>(values[3] - values[2]) * scale, ++timing_sample_id_};
    for (std::uint32_t region = 0; region < slot.region_count; ++region)
    {
        const auto* pair = values + 4 + 2 * region;
        timings.regions[region] = {slot.region_labels[region], static_cast<double>(pair[1] - pair[0]) * scale};
    }
    timings.region_count = slot.region_count;
    gpu_timings_ = timings;
    slot.timestamps_pending = false;
    return true;
}

UINT D3D12Renderer::begin_region(const char* label, const char* fallback)
{
    auto& slot = frames_[frame_index_];
    if (!regions_ || slot.region_count == max_gpu_regions) return no_region;
    const auto region = slot.region_count++;
    slot.region_labels[region] = label ? label : fallback;
    command_list_->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
        static_cast<UINT>(frame_index_) * queries_per_slot + 4 + 2 * region);
    return region;
}

void D3D12Renderer::end_region(UINT region)
{
    if (region == no_region) return;
    command_list_->EndQuery(timestamps_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
        static_cast<UINT>(frame_index_) * queries_per_slot + 5 + 2 * region);
}
}
