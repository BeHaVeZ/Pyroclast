#include "d3d12_renderer.hpp"
#include "d3d12_resources.hpp"

#include <SDL3/SDL.h>

namespace pyroclast
{
std::unique_ptr<ComputePipeline> D3D12Renderer::create_compute_pipeline(
    std::span<const std::byte> shader, const VertexBuffer& resource)
{
    const auto* vertices = dynamic_cast<const D3D12VertexBuffer*>(&resource);
    if (!vertices || vertices->owner != this || shader.empty())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Compute requires shader bytecode and a buffer belonging to this D3D12 device.");
        return nullptr;
    }
    auto pipeline = std::make_unique<D3D12ComputePipeline>();
    pipeline->owner = this;
    pipeline->vertices = vertices;
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants.ShaderRegister = 0;
    parameters[1].Constants.Num32BitValues = 2;
    D3D12_ROOT_SIGNATURE_DESC root{};
    root.NumParameters = 2;
    root.pParameters = parameters;
    Microsoft::WRL::ComPtr<ID3DBlob> serialized;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT result = D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
    if (FAILED(result) && errors)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Compute root signature: %s",
            static_cast<const char*>(errors->GetBufferPointer()));
    }
    if (!check(result, "Serialize compute root signature") ||
        !check(device_->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
            IID_PPV_ARGS(&pipeline->root_signature)), "Create compute root signature"))
    {
        return nullptr;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
    description.pRootSignature = pipeline->root_signature.Get();
    description.CS = {shader.data(), shader.size()};
    if (!check(device_->CreateComputePipelineState(&description, IID_PPV_ARGS(&pipeline->state)),
        "Create compute pipeline") || !check_validation())
    {
        return nullptr;
    }
    return pipeline;
}
}
