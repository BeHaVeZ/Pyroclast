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
    using VertexBuffer::VertexBuffer;

    const D3D12Renderer* owner = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    D3D12_VERTEX_BUFFER_VIEW view{};
};

class D3D12GraphicsPipeline final : public GraphicsPipeline
{
public:
    using GraphicsPipeline::GraphicsPipeline;

    const D3D12Renderer* owner = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> state;
};

class D3D12ComputePipeline final : public ComputePipeline
{
public:
    using ComputePipeline::ComputePipeline;

    const D3D12Renderer* owner = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> state;
};
}
