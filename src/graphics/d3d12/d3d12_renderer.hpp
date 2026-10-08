#pragma once

#include "../graphics_device.hpp"

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>

#include <array>
#include <cstdint>
#include <vector>

struct SDL_Window;

namespace pyroclast
{
class D3D12Buffer;

class D3D12Renderer final : public GraphicsDevice
{
public:
    D3D12Renderer() = default;
    ~D3D12Renderer() override;
    D3D12Renderer(const D3D12Renderer&) = delete;
    D3D12Renderer& operator=(const D3D12Renderer&) = delete;

    bool initialize(SDL_Window* window, const GraphicsDeviceOptions& options);
    std::unique_ptr<Buffer> create_buffer(std::uint32_t size, std::span<const std::byte> initial) override;
    std::unique_ptr<GraphicsPipeline> create_graphics_pipeline(
        const GraphicsPipelineDescription& description) override;
    bool resize(int width, int height) override;
    std::unique_ptr<ComputePipeline> create_compute_pipeline(Shader shader) override;
    std::unique_ptr<RenderTarget> create_render_target(const RenderTargetDescription& description) override;
    RenderResult render(const Frame& frame) override;
    bool wait_idle() override;
    std::optional<GpuTimings> completed_gpu_timings() const override { return gpu_timings_; }
    std::optional<ReadbackData> completed_readback() const override { return readback_; }
    std::optional<CapturedImage> read_capture() override;
    std::string_view adapter_name() const override { return adapter_name_; }
    SubmitTimings last_submit_timings() const override { return submit_timings_; }
    PresentInfo present_info() const override
    {
        return {vsync_ ? "flip-discard interval 1" : tearing_ ? "flip-discard interval 0 tearing" : "flip-discard interval 0", backbuffer_count};
    }
    std::optional<GpuMemory> memory_usage() const override;

private:
    static constexpr UINT frame_count = 2;
    static constexpr UINT backbuffer_count = 2;
    static constexpr DXGI_FORMAT backbuffer_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    static constexpr DXGI_FORMAT depth_format = DXGI_FORMAT_D32_FLOAT;
    static constexpr DXGI_FORMAT color_format(TargetFormat format)
    {
        return format == TargetFormat::rgba16_float ? DXGI_FORMAT_R16G16B16A16_FLOAT :
            format == TargetFormat::r11g11b10_float ? DXGI_FORMAT_R11G11B10_FLOAT : backbuffer_format;
    }
    static constexpr D3D12_RESOURCE_STATES color_resting_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    static constexpr D3D12_RESOURCE_STATES depth_resting_state =
        D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    // Shader-visible descriptors: a null SRV, then render target SRVs, then Dear ImGui's textures.
    static constexpr UINT shader_descriptor_capacity = 64;
    static constexpr UINT overlay_descriptor_first = 48;

    enum RootParameter : UINT
    {
        root_draw_constants = 0,
        root_frame_constants = 1,
        root_read_buffers = 2,
        root_write_buffers = root_read_buffers + binding_layout::read_buffer_slots,
        root_textures = root_write_buffers + binding_layout::write_buffer_slots,
        root_parameter_count = root_textures + binding_layout::texture_slots
    };

    static constexpr UINT queries_per_slot = 4 + 2 * max_gpu_regions;
    static constexpr UINT no_region = ~0u;

    struct FrameSlot
    {
        bool timestamps_pending = false;
        std::array<const char*, max_gpu_regions> region_labels{};
        std::uint32_t region_count = 0;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        std::uint64_t fence_value = 0;
        Microsoft::WRL::ComPtr<ID3D12Resource> constants;
        std::byte* mapped_constants = nullptr;
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        const std::byte* mapped_readback = nullptr;
        std::uint32_t readback_size = 0;
        bool readback_pending = false;
    };

    class BufferStates
    {
    public:
        void require(ID3D12Resource* buffer, D3D12_RESOURCE_STATES state);
        void flush(ID3D12GraphicsCommandList* commands);
        void reset() { states_.clear(); }

    private:
        std::vector<std::pair<ID3D12Resource*, D3D12_RESOURCE_STATES>> states_;
        std::vector<D3D12_RESOURCE_BARRIER> pending_;
    };

    bool create_bindings();
    bool size_target(class D3D12RenderTarget& target);
    bool owns(const Frame& frame) const;
    void bind(const Bindings& bindings, bool graphics);
    void draw(const Draw& draw);
    void set_target(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const D3D12_CPU_DESCRIPTOR_HANDLE* dsv, int width, int height);
    bool record_capture(ID3D12Resource* backbuffer);
    bool create_overlay();
    void destroy_overlay();
    void record_overlay(ImDrawData* overlay, D3D12_CPU_DESCRIPTOR_HANDLE backbuffer_view);
    bool create_device(bool gpu_validation, std::string_view requested_adapter);
    bool create_timestamps();
    UINT begin_region(const char* label, const char* fallback);
    void end_region(UINT region);
    bool read_timestamps(std::size_t frame);
    void read_readback(FrameSlot& slot);
    bool create_swapchain(SDL_Window* window);
    bool create_render_targets();
    bool wait_for_fence(std::uint64_t value);
    bool check(HRESULT result, const char* operation);
    bool check_validation();
    D3D12_GPU_DESCRIPTOR_HANDLE shader_descriptor(UINT index) const;

    std::string adapter_name_;
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory_;
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> info_queue_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    Microsoft::WRL::Wrappers::Event fence_event_;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    std::array<FrameSlot, frame_count> frames_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, backbuffer_count> backbuffers_;
    std::size_t frame_index_ = 0;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
    Microsoft::WRL::ComPtr<ID3D12Resource> upload_staging_;
    Microsoft::WRL::ComPtr<ID3D12Resource> pending_upload_buffer_;
    std::uint64_t next_fence_value_ = 1;
    UINT rtv_stride_ = 0;
    UINT dsv_stride_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool occluded_ = false;
    bool vsync_ = true;
    bool tearing_ = false;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> shader_heap_;
    UINT shader_descriptor_stride_ = 0;
    UINT next_shader_descriptor_ = 1;
    BufferStates buffer_states_;
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> draw_signature_;
    std::optional<ReadbackData> readback_;
    std::uint64_t readback_sample_id_ = 0;
    bool regions_ = false;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> timestamps_;
    Microsoft::WRL::ComPtr<ID3D12Resource> timestamp_readback_;
    const std::uint64_t* mapped_timestamps_ = nullptr;
    std::uint64_t timestamp_frequency_ = 0;
    std::optional<GpuTimings> gpu_timings_;
    std::uint64_t timing_sample_id_ = 0;
    SubmitTimings submit_timings_;
    bool overlay_ = false;
    std::vector<UINT> overlay_descriptors_;
    bool capture_enabled_ = false;
    bool capture_pending_ = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> capture_readback_;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT capture_footprint_{};
    UINT64 capture_size_ = 0;
    std::uint64_t validation_warnings_ = 0;
    std::uint64_t validation_errors_ = 0;
};
}
