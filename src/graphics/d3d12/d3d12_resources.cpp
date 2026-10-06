#include "d3d12_renderer.hpp"

#include "d3d12_resources.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstring>
#include <limits>

namespace pyroclast
{
std::unique_ptr<GraphicsPipeline> D3D12Renderer::create_graphics_pipeline(
    const GraphicsPipelineDescription& shaders)
{
    if (shaders.vertex_shader.empty() || shaders.pixel_shader.empty())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Graphics pipeline requires both shader stages.");
        return nullptr;
    }
    auto pipeline = std::make_unique<D3D12GraphicsPipeline>();
    pipeline->owner = this;
    pipeline->vertex_layout = shaders.layout;
    const bool particles = shaders.layout == VertexLayout::particle;
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_0};
    if (!check(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,
        &shader_model, sizeof(shader_model)), "Query shader model support"))
    {
        return nullptr;
    }
    if (shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "The triangle requires shader model 6.0.");
        return nullptr;
    }

    D3D12_ROOT_SIGNATURE_DESC root_description{};
    D3D12_ROOT_PARAMETER aspect{};
    aspect.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    aspect.Constants.Num32BitValues = 2;
    aspect.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    root_description.NumParameters = particles ? 1 : 0;
    root_description.pParameters = particles ? &aspect : nullptr;
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
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
            static_cast<UINT>(offsetof(Vertex, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
            static_cast<UINT>(offsetof(Vertex, color)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };
    const D3D12_INPUT_ELEMENT_DESC particle_layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1}
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature = pipeline->root_signature.Get();
    description.VS = {shaders.vertex_shader.data(), shaders.vertex_shader.size()};
    description.PS = {shaders.pixel_shader.data(), shaders.pixel_shader.size()};
    description.InputLayout = {particles ? particle_layout : input_layout, 2};
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.NumRenderTargets = 1;
    description.RTVFormats[0] = backbuffer_format;
    description.SampleDesc.Count = 1;
    description.SampleMask = std::numeric_limits<UINT>::max();
    description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    description.RasterizerState.CullMode = particles ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
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

std::unique_ptr<VertexBuffer> D3D12Renderer::create_vertex_buffer(std::span<const std::byte> vertices, VertexLayout layout)
{
    if (vertices.empty() || vertices.size() % vertex_stride(layout) != 0 || vertices.size_bytes() > std::numeric_limits<UINT>::max())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Vertex buffer must have a nonzero size fitting a 32-bit buffer view.");
        return nullptr;
    }
    auto buffer = std::make_unique<D3D12VertexBuffer>();
    buffer->owner = this;
    buffer->count = static_cast<UINT>(vertices.size() / vertex_stride(layout));
    buffer->layout = layout;
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = vertices.size_bytes();
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
        &description, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&buffer->resource)), "Create vertex buffer"))
    {
        return nullptr;
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
    std::memcpy(mapped, vertices.data(), vertices.size_bytes());
    const D3D12_RANGE written_range{0, vertices.size_bytes()};
    upload_staging_->Unmap(0, &written_range);

    if (!wait_idle() || !check(frames_[0].allocator->Reset(), "Reset upload allocator") ||
        !check(command_list_->Reset(frames_[0].allocator.Get(), nullptr), "Reset upload command list"))
    {
        return nullptr;
    }
    command_list_->CopyBufferRegion(buffer->resource.Get(), 0, upload_staging_.Get(), 0, vertices.size_bytes());
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = buffer->resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    command_list_->ResourceBarrier(1, &barrier);
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
    buffer->view.BufferLocation = buffer->resource->GetGPUVirtualAddress();
    buffer->view.SizeInBytes = static_cast<UINT>(vertices.size_bytes());
    buffer->view.StrideInBytes = vertex_stride(layout);
    SDL_Log("Vertex buffer uploaded: %u vertices.", buffer->count);
    return buffer;
}
}
