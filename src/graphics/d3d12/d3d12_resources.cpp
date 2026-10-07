#include "d3d12_renderer.hpp"

#include "d3d12_resources.hpp"
#include "d3d12_shaders.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstring>
#include <limits>

namespace pyroclast
{
std::unique_ptr<GraphicsPipeline> D3D12Renderer::create_graphics_pipeline(
    const GraphicsPipelineDescription& shaders)
{
    const auto vertex_shader = d3d12_shader(shaders.vertex_shader);
    const auto pixel_shader = d3d12_shader(shaders.pixel_shader);
    auto pipeline = std::make_unique<D3D12GraphicsPipeline>(shaders.blend);
    pipeline->owner = this;
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_0};
    if (!check(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,
        &shader_model, sizeof(shader_model)), "Query shader model support"))
    {
        return nullptr;
    }
    if (shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Particle rendering requires shader model 6.0.");
        return nullptr;
    }

    D3D12_ROOT_SIGNATURE_DESC root_description{};
    D3D12_ROOT_PARAMETER view{};
    view.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    view.Constants.Num32BitValues = 2;
    view.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    root_description.NumParameters = 1;
    root_description.pParameters = &view;
    root_description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    Microsoft::WRL::ComPtr<ID3DBlob> serialized_root;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT result = D3D12SerializeRootSignature(&root_description,
        D3D_ROOT_SIGNATURE_VERSION_1, &serialized_root, &errors);
    if (FAILED(result) && errors)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Root signature: %s",
            static_cast<const char*>(errors->GetBufferPointer()));
    }
    if (!check(result, "Serialize root signature") ||
        !check(device_->CreateRootSignature(0, serialized_root->GetBufferPointer(),
            serialized_root->GetBufferSize(), IID_PPV_ARGS(&pipeline->root_signature)),
            "Create root signature"))
    {
        return nullptr;
    }

    const D3D12_INPUT_ELEMENT_DESC input_layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, particle_layout::particle_motion_offset,
            D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, particle_layout::particle_life_offset,
            D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1}
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature = pipeline->root_signature.Get();
    description.VS = {vertex_shader.data(), vertex_shader.size()};
    description.PS = {pixel_shader.data(), pixel_shader.size()};
    description.InputLayout = {input_layout, 2};
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.NumRenderTargets = 1;
    description.RTVFormats[0] = backbuffer_format;
    description.SampleDesc.Count = 1;
    description.SampleMask = std::numeric_limits<UINT>::max();
    description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    description.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    description.RasterizerState.DepthClipEnable = TRUE;
    description.DepthStencilState.DepthEnable = FALSE;
    description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    auto& blend = description.BlendState.RenderTarget[0];
    blend.BlendEnable = shaders.blend != BlendMode::opaque;
    blend.SrcBlend = D3D12_BLEND_ONE;
    blend.DestBlend = shaders.blend == BlendMode::premultiplied_alpha ? D3D12_BLEND_INV_SRC_ALPHA :
        shaders.blend == BlendMode::additive ? D3D12_BLEND_ONE : D3D12_BLEND_ZERO;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = blend.DestBlend;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    if (!check(device_->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&pipeline->state)),
        "Create graphics pipeline") || !check_validation())
    {
        return nullptr;
    }
    return pipeline;
}

std::unique_ptr<Buffer> D3D12Renderer::create_buffer(std::uint32_t size, std::span<const std::byte> initial)
{
    if (size == 0 || size % 4 != 0 || (!initial.empty() && initial.size() != size))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Buffer size must be a nonzero multiple of four bytes and match its initial data.");
        return nullptr;
    }
    auto buffer = std::make_unique<D3D12Buffer>(size);
    buffer->owner = this;
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = size;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    if (!check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
        &description, D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(&buffer->resource)), "Create buffer"))
    {
        return nullptr;
    }
    if (initial.empty())
    {
        return check_validation() ? std::move(buffer) : nullptr;
    }
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    description.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (!check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
        &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&upload_staging_)), "Create upload buffer"))
    {
        return nullptr;
    }

    void* mapped = nullptr;
    const D3D12_RANGE read_range{0, 0};
    if (!check(upload_staging_->Map(0, &read_range, &mapped), "Map upload buffer"))
    {
        return nullptr;
    }
    std::memcpy(mapped, initial.data(), initial.size_bytes());
    const D3D12_RANGE written_range{0, initial.size_bytes()};
    upload_staging_->Unmap(0, &written_range);

    if (!wait_idle() || !check(frames_[0].allocator->Reset(), "Reset upload allocator") ||
        !check(command_list_->Reset(frames_[0].allocator.Get(), nullptr), "Reset upload command list"))
    {
        return nullptr;
    }
    command_list_->CopyBufferRegion(buffer->resource.Get(), 0, upload_staging_.Get(), 0, initial.size_bytes());
    if (!check(command_list_->Close(), "Close upload command list"))
    {
        return nullptr;
    }
    ID3D12CommandList* lists[] = {command_list_.Get()};
    pending_upload_buffer_ = buffer->resource;
    queue_->ExecuteCommandLists(1, lists);

    if (!wait_idle())
    {
        return nullptr;
    }
    upload_staging_.Reset();
    pending_upload_buffer_.Reset();
    SDL_Log("Buffer uploaded: %u bytes.", size);
    return buffer;
}
}
