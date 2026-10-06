#pragma once

#include "../graphics_device.hpp"

#include <d3d12.h>
#include <wrl/client.h>

namespace pyroclast
{
class D3D12Renderer;

class D3D12VertexBuffer final : public VertexBuffer
{
public:
    std::uint32_t vertex_count() const override { return count; }

    const D3D12Renderer* owner = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    D3D12_VERTEX_BUFFER_VIEW view{};
    std::uint32_t count = 0;
};

class D3D12GraphicsPipeline final : public GraphicsPipeline
{
public:
    const D3D12Renderer* owner = nullptr;
    BlendMode blend = BlendMode::opaque;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> state;
};

class D3D12ComputePipeline final : public ComputePipeline
{
public:
    const D3D12Renderer* owner = nullptr;
    const D3D12VertexBuffer* vertices = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> state;
};
}
