#include "d3d12_renderer.hpp"
#include "d3d12_resources.hpp"
#include "d3d12_shaders.hpp"

#include <SDL3/SDL.h>
#include <limits>

namespace pyroclast
{
bool D3D12Renderer::create_upscale_pipeline()
{
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
    if (!check(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors), "Serialize upscale root") ||
        !check(device_->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
            IID_PPV_ARGS(&upscale_root_)), "Create upscale root")) return false;

    const auto vertex_shader = d3d12_shader(Shader::upscale_vertex);
    const auto pixel_shader = d3d12_shader(Shader::upscale_pixel);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = upscale_root_.Get();
    pipeline.VS = {vertex_shader.data(), vertex_shader.size()};
    pipeline.PS = {pixel_shader.data(), pixel_shader.size()};
    pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = backbuffer_format;
    pipeline.SampleDesc.Count = 1;
    pipeline.SampleMask = std::numeric_limits<UINT>::max();
    pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipeline.RasterizerState.DepthClipEnable = TRUE;
    pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    return check(device_->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&upscale_pipeline_)), "Create upscale pipeline");
}

std::unique_ptr<RenderTarget> D3D12Renderer::create_render_target(const RenderTargetDescription& description)
{
    if (description.divisor == 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Render target divisor must be at least one.");
        return nullptr;
    }
    if (!upscale_pipeline_ && !create_upscale_pipeline()) return nullptr;
    auto target = std::make_unique<D3D12RenderTarget>(description);
    target->owner = this;
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.NumDescriptors = 1;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (!check(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&target->render_view)), "Create render target view heap")) return nullptr;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (!check(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&target->shader_view)), "Create render target sampling heap")) return nullptr;
    return check_validation() ? std::move(target) : nullptr;
}

bool D3D12Renderer::size_target(D3D12RenderTarget& target)
{
    const auto divisor = static_cast<int>(target.description().divisor);
    const int width = (width_ + divisor - 1) / divisor;
    const int height = (height_ + divisor - 1) / divisor;
    if (target.texture && target.width == width && target.height == height)
    {
        return true;
    }
    if (!wait_idle())
    {
        return false;
    }
    D3D12_RESOURCE_DESC texture{};
    texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width = static_cast<UINT64>(width);
    texture.Height = static_cast<UINT>(height);
    texture.DepthOrArraySize = 1;
    texture.MipLevels = 1;
    texture.Format = backbuffer_format;
    texture.SampleDesc.Count = 1;
    texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    const auto& color = target.description().clear_color;
    const D3D12_CLEAR_VALUE clear{backbuffer_format, {color[0], color[1], color[2], color[3]}};
    if (!check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&target.texture)), "Create render target")) return false;
    device_->CreateRenderTargetView(target.texture.Get(), nullptr, target.render_view->GetCPUDescriptorHandleForHeapStart());
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = backbuffer_format;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(target.texture.Get(), &view, target.shader_view->GetCPUDescriptorHandleForHeapStart());
    target.width = width;
    target.height = height;
    SDL_Log("D3D12 render target: %dx%d, RGBA8.", width, height);
    return check_validation();
}

void D3D12Renderer::upscale(const D3D12RenderTarget& source)
{
    ID3D12DescriptorHeap* heaps[] = {source.shader_view.Get()};
    command_list_->SetDescriptorHeaps(1, heaps);
    command_list_->SetPipelineState(upscale_pipeline_.Get());
    command_list_->SetGraphicsRootSignature(upscale_root_.Get());
    command_list_->SetGraphicsRootDescriptorTable(0, source.shader_view->GetGPUDescriptorHandleForHeapStart());
    command_list_->DrawInstanced(3, 1, 0, 0);
}
}
