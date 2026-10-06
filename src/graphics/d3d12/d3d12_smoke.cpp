#include "d3d12_renderer.hpp"
#include "d3d12_resources.hpp"

#include <SDL3/SDL.h>
#include <limits>
#include <utility>

namespace
{
#ifdef NDEBUG
#include "d3d12/Release/smoke_composite_vs.hpp"
#include "d3d12/Release/smoke_composite_ps.hpp"
#else
#include "d3d12/Debug/smoke_composite_vs.hpp"
#include "d3d12/Debug/smoke_composite_ps.hpp"
#endif
}

namespace pyroclast
{
bool D3D12Renderer::create_smoke_pipeline()
{
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.NumDescriptors = 1;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (!check(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&smoke_rtv_)), "Create smoke RTV heap")) return false;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (!check(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&smoke_srv_)), "Create smoke SRV heap")) return false;

    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable = {1, &range};
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC root{};
    root.NumParameters = 1;
    root.pParameters = &parameter;
    root.NumStaticSamplers = 1;
    root.pStaticSamplers = &sampler;
    Microsoft::WRL::ComPtr<ID3DBlob> serialized, errors;
    if (!check(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors), "Serialize smoke composite root") ||
        !check(device_->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
            IID_PPV_ARGS(&smoke_root_)), "Create smoke composite root")) return false;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = smoke_root_.Get();
    pipeline.VS = {smoke_composite_vs, sizeof(smoke_composite_vs)};
    pipeline.PS = {smoke_composite_ps, sizeof(smoke_composite_ps)};
    pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = backbuffer_format;
    pipeline.SampleDesc.Count = 1;
    pipeline.SampleMask = std::numeric_limits<UINT>::max();
    pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipeline.RasterizerState.DepthClipEnable = TRUE;
    pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    return check(device_->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&smoke_composite_)), "Create smoke composite pipeline");
}

bool D3D12Renderer::create_smoke_target()
{
    D3D12_RESOURCE_DESC texture{};
    texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width = (static_cast<UINT>(width_) + 1) / 2;
    texture.Height = (static_cast<UINT>(height_) + 1) / 2;
    texture.DepthOrArraySize = 1;
    texture.MipLevels = 1;
    texture.Format = backbuffer_format;
    texture.SampleDesc.Count = 1;
    texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    const D3D12_CLEAR_VALUE clear{backbuffer_format, {0.04f, 0.07f, 0.12f, 1.0f}};
    if (!check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&smoke_target_)), "Create half-resolution smoke target")) return false;
    device_->CreateRenderTargetView(smoke_target_.Get(), nullptr, smoke_rtv_->GetCPUDescriptorHandleForHeapStart());
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = backbuffer_format;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(smoke_target_.Get(), &view, smoke_srv_->GetCPUDescriptorHandleForHeapStart());
    SDL_Log("D3D12 smoke target: %llux%u, RGBA8; includes clear background.", texture.Width, texture.Height);
    return check_validation();
}

void D3D12Renderer::set_target(D3D12_CPU_DESCRIPTOR_HANDLE rtv, int width, int height)
{
    command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    const D3D12_RECT scissor{0, 0, width, height};
    command_list_->RSSetViewports(1, &viewport);
    command_list_->RSSetScissorRects(1, &scissor);
}

void D3D12Renderer::draw_range(const DrawRange& range, const D3D12VertexBuffer& vertices, float aspect)
{
    const auto& pipeline = static_cast<const D3D12GraphicsPipeline&>(range.pipeline);
    command_list_->SetPipelineState(pipeline.state.Get());
    command_list_->SetGraphicsRootSignature(pipeline.root_signature.Get());
    auto view = vertices.view;
    view.BufferLocation += static_cast<UINT64>(range.first_record) * view.StrideInBytes;
    view.SizeInBytes = range.record_count * view.StrideInBytes;
    command_list_->IASetVertexBuffers(0, 1, &view);
    command_list_->SetGraphicsRoot32BitConstants(0, 1, &aspect, 0);
    command_list_->SetGraphicsRoot32BitConstants(0, 1, &range.first_record, 1);
    command_list_->DrawInstanced(6, range.record_count, 0, 0);
}

void D3D12Renderer::draw_smoke(const DrawRange& range, const D3D12VertexBuffer& vertices,
    const DrawCommand& draw, D3D12_CPU_DESCRIPTOR_HANDLE backbuffer)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = smoke_target_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    command_list_->ResourceBarrier(1, &barrier);
    auto rtv = smoke_rtv_->GetCPUDescriptorHandleForHeapStart();
    set_target(rtv, (width_ + 1) / 2, (height_ + 1) / 2);
    command_list_->ClearRenderTargetView(rtv, draw.clear_color.data(), 0, nullptr);
    draw_range(range, vertices, draw.aspect_ratio);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &barrier);
    set_target(backbuffer, width_, height_);
    ID3D12DescriptorHeap* heaps[] = {smoke_srv_.Get()};
    command_list_->SetDescriptorHeaps(1, heaps);
    command_list_->SetPipelineState(smoke_composite_.Get());
    command_list_->SetGraphicsRootSignature(smoke_root_.Get());
    command_list_->SetGraphicsRootDescriptorTable(0, smoke_srv_->GetGPUDescriptorHandleForHeapStart());
    command_list_->DrawInstanced(3, 1, 0, 0);
}
}
