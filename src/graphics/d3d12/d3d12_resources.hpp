#pragma once

#include "../graphics_device.hpp"

#include <d3d12.h>
#include <wrl/client.h>

namespace pyroclast
{
class D3D12Renderer;

class D3D12Buffer final : public Buffer
{
public:
    using Buffer::Buffer;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
};

class D3D12GraphicsPipeline final : public GraphicsPipeline
{
public:
    using GraphicsPipeline::GraphicsPipeline;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> state;
    D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};

class D3D12RenderTarget final : public RenderTarget
{
public:
    using RenderTarget::RenderTarget;

    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> render_view;
    UINT shader_view = 0;
    int width = 0;
    int height = 0;
};

class D3D12ComputePipeline final : public ComputePipeline
{
public:
    using ComputePipeline::ComputePipeline;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> state;
};
}
