#pragma once

#include "../graphics_device.hpp"

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>

#include <array>
#include <cstdint>

struct SDL_Window;

namespace pyroclast
{
class D3D12Renderer final : public GraphicsDevice
{
public:
    D3D12Renderer() = default;
    ~D3D12Renderer() override;
    D3D12Renderer(const D3D12Renderer&) = delete;
    D3D12Renderer& operator=(const D3D12Renderer&) = delete;

    bool initialize(SDL_Window* window, const GraphicsDeviceOptions& options);
    std::unique_ptr<VertexBuffer> create_vertex_buffer(std::span<const std::byte> vertices, std::uint32_t stride) override;
    std::unique_ptr<GraphicsPipeline> create_graphics_pipeline(
        const GraphicsPipelineDescription& description) override;
    bool resize(int width, int height) override;
    std::unique_ptr<ComputePipeline> create_compute_pipeline(
        std::span<const std::byte> shader, const VertexBuffer& vertices) override;
    RenderResult render(const DrawCommand& draw) override;
    bool wait_idle() override;
    std::optional<GpuTimings> completed_gpu_timings() const override { return gpu_timings_; }

private:
    static constexpr UINT frame_count = 2;
    static constexpr DXGI_FORMAT backbuffer_format = DXGI_FORMAT_R8G8B8A8_UNORM;

    struct Frame
    {
        bool timestamps_pending = false;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        Microsoft::WRL::ComPtr<ID3D12Resource> backbuffer;
        std::uint64_t fence_value = 0;
    };

    bool create_smoke_pipeline();
    bool create_smoke_target();
    void draw_range(const DrawRange& range, const class D3D12VertexBuffer& vertices, float aspect);
    void set_target(D3D12_CPU_DESCRIPTOR_HANDLE rtv, int width, int height);
    void draw_smoke(const DrawRange& range, const class D3D12VertexBuffer& vertices, const DrawCommand& draw, D3D12_CPU_DESCRIPTOR_HANDLE backbuffer);
    bool create_device();
    bool create_timestamps();
    bool read_timestamps(std::size_t frame);
    bool create_swapchain(SDL_Window* window);
    bool create_render_targets();
    bool wait_for_fence(std::uint64_t value);
    bool check(HRESULT result, const char* operation);
    bool check_validation();

    Microsoft::WRL::ComPtr<IDXGIFactory6> factory_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> info_queue_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    Microsoft::WRL::Wrappers::Event fence_event_;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    std::array<Frame, frame_count> frames_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
    Microsoft::WRL::ComPtr<ID3D12Resource> upload_staging_;
    Microsoft::WRL::ComPtr<ID3D12Resource> pending_upload_buffer_;
    std::uint64_t next_fence_value_ = 1;
    UINT rtv_stride_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool occluded_ = false;
    bool vsync_ = true;
    bool half_smoke_ = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> smoke_target_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> smoke_rtv_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> smoke_srv_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> smoke_root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> smoke_composite_;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> timestamps_;
    Microsoft::WRL::ComPtr<ID3D12Resource> timestamp_readback_;
    const std::uint64_t* mapped_timestamps_ = nullptr;
    std::uint64_t timestamp_frequency_ = 0;
    std::optional<GpuTimings> gpu_timings_;
    std::uint64_t timing_sample_id_ = 0;
    std::uint64_t validation_warnings_ = 0;
    std::uint64_t validation_errors_ = 0;
};
}
