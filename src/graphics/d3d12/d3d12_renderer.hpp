#pragma once

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
class D3D12Renderer final
{
public:
    D3D12Renderer() = default;
    ~D3D12Renderer();
    D3D12Renderer(const D3D12Renderer&) = delete;
    D3D12Renderer& operator=(const D3D12Renderer&) = delete;

    bool initialize(SDL_Window* window);
    bool resize(int width, int height);
    bool render();
    bool wait_idle();

private:
    static constexpr UINT frame_count = 2;
    static constexpr DXGI_FORMAT backbuffer_format = DXGI_FORMAT_R8G8B8A8_UNORM;

    struct Frame
    {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        Microsoft::WRL::ComPtr<ID3D12Resource> backbuffer;
        std::uint64_t fence_value = 0;
    };

    bool create_device();
    bool create_swapchain(SDL_Window* window);
    bool create_render_targets();
    bool create_triangle_pipeline();
    bool upload_triangle();
    void draw_triangle();
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
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertex_buffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> triangle_upload_;
    D3D12_VERTEX_BUFFER_VIEW vertex_view_{};
    std::uint64_t next_fence_value_ = 1;
    UINT rtv_stride_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool occluded_ = false;
    std::uint64_t validation_warnings_ = 0;
    std::uint64_t validation_errors_ = 0;
};
}
