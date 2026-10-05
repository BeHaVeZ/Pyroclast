#include "d3d12_renderer.hpp"

#include "triangle_vs.hpp"
#include "triangle_ps.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstring>
#include <limits>

namespace pyroclast
{
namespace
{
struct Vertex
{
    float position[3];
    float color[3];
};
static_assert(sizeof(Vertex) == 24 && offsetof(Vertex, color) == 12);
constexpr Vertex vertices[] = {
    {{0.0f, 0.6f, 0.0f}, {1.0f, 0.0f, 0.0f}},
    {{0.6f, -0.6f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{-0.6f, -0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}}
};
}

bool D3D12Renderer::create_triangle_pipeline()
{
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_0};
    if (!check(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,
        &shader_model, sizeof(shader_model)), "Query shader model support"))
    {
        return false;
    }
    if (shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "The triangle requires shader model 6.0.");
        return false;
    }

    D3D12_ROOT_SIGNATURE_DESC root_description{};
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
            serialized_root->GetBufferSize(), IID_PPV_ARGS(&root_signature_)),
            "Create root signature"))
    {
        return false;
    }

    const D3D12_INPUT_ELEMENT_DESC input_layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
            static_cast<UINT>(offsetof(Vertex, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
            static_cast<UINT>(offsetof(Vertex, color)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature = root_signature_.Get();
    description.VS = {triangle_vs, sizeof(triangle_vs)};
    description.PS = {triangle_ps, sizeof(triangle_ps)};
    description.InputLayout = {input_layout, 2};
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.NumRenderTargets = 1;
    description.RTVFormats[0] = backbuffer_format;
    description.SampleDesc.Count = 1;
    description.SampleMask = std::numeric_limits<UINT>::max();
    description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    description.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    description.RasterizerState.DepthClipEnable = TRUE;
    description.DepthStencilState.DepthEnable = FALSE;
    description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    auto& blend = description.BlendState.RenderTarget[0];
    blend.SrcBlend = D3D12_BLEND_ONE;
    blend.DestBlend = D3D12_BLEND_ZERO;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    return check(device_->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&pipeline_)),
        "Create triangle graphics pipeline") && check_validation();
}

bool D3D12Renderer::upload_triangle()
{
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = sizeof(vertices);
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;
    if (!check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
        &description, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&vertex_buffer_)), "Create triangle vertex buffer"))
    {
        return false;
    }
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    if (!check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
        &description, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&triangle_upload_)), "Create triangle upload buffer"))
    {
        return false;
    }

    void* mapped = nullptr;
    const D3D12_RANGE read_range{0, 0};
    if (!check(triangle_upload_->Map(0, &read_range, &mapped), "Map triangle upload buffer"))
    {
        return false;
    }
    std::memcpy(mapped, vertices, sizeof(vertices));
    const D3D12_RANGE written_range{0, sizeof(vertices)};
    triangle_upload_->Unmap(0, &written_range);

    if (!check(frames_[0].allocator->Reset(), "Reset upload allocator") ||
        !check(command_list_->Reset(frames_[0].allocator.Get(), nullptr), "Reset upload command list"))
    {
        return false;
    }
    command_list_->CopyBufferRegion(vertex_buffer_.Get(), 0, triangle_upload_.Get(), 0, sizeof(vertices));
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = vertex_buffer_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    command_list_->ResourceBarrier(1, &barrier);
    if (!check(command_list_->Close(), "Close upload command list"))
    {
        return false;
    }
    ID3D12CommandList* lists[] = {command_list_.Get()};
    queue_->ExecuteCommandLists(1, lists);

    if (!wait_idle())
    {
        return false;
    }
    triangle_upload_.Reset();
    vertex_view_.BufferLocation = vertex_buffer_->GetGPUVirtualAddress();
    vertex_view_.SizeInBytes = static_cast<UINT>(sizeof(vertices));
    vertex_view_.StrideInBytes = sizeof(Vertex);
    SDL_Log("Triangle initialized: three vertices, shader model 6.0.");
    return true;
}

void D3D12Renderer::draw_triangle()
{
    const D3D12_VIEWPORT viewport{0.0f, 0.0f,
        static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, width_, height_};
    command_list_->SetPipelineState(pipeline_.Get());
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    command_list_->RSSetViewports(1, &viewport);
    command_list_->RSSetScissorRects(1, &scissor);
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->IASetVertexBuffers(0, 1, &vertex_view_);
    command_list_->DrawInstanced(3, 1, 0, 0);
}
}
