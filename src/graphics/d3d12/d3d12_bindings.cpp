#include "d3d12_renderer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

namespace pyroclast
{
namespace
{
D3D12_STATIC_SAMPLER_DESC clamp_sampler(UINT shader_register, D3D12_FILTER filter)
{
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = filter;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = shader_register;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    return sampler;
}
}

bool D3D12Renderer::create_bindings()
{
    D3D12_FEATURE_DATA_ROOT_SIGNATURE version{D3D_ROOT_SIGNATURE_VERSION_1_1};
    if (FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &version, sizeof(version))) ||
        version.HighestVersion < D3D_ROOT_SIGNATURE_VERSION_1_1)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "D3D12 root signature version 1.1 is required.");
        return false;
    }
    std::array<D3D12_ROOT_PARAMETER1, root_parameter_count> parameters{};
    auto& constants = parameters[root_draw_constants];
    constants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    constants.Constants = {0, 0, binding_layout::draw_constant_words};
    auto& frame_constants = parameters[root_frame_constants];
    frame_constants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    frame_constants.Descriptor = {1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE};
    for (UINT slot = 0; slot < binding_layout::read_buffer_slots; ++slot)
    {
        parameters[root_read_buffers + slot].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[root_read_buffers + slot].Descriptor = {slot, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE};
    }
    for (UINT slot = 0; slot < binding_layout::write_buffer_slots; ++slot)
    {
        parameters[root_write_buffers + slot].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        parameters[root_write_buffers + slot].Descriptor = {slot, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE};
    }
    std::array<D3D12_DESCRIPTOR_RANGE1, binding_layout::texture_slots> textures{};
    for (UINT slot = 0; slot < binding_layout::texture_slots; ++slot)
    {
        textures[slot] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, binding_layout::read_buffer_slots + slot, 0,
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, 0};
        parameters[root_textures + slot].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[root_textures + slot].DescriptorTable = {1, &textures[slot]};
    }
    const D3D12_STATIC_SAMPLER_DESC samplers[] = {
        clamp_sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR), clamp_sampler(1, D3D12_FILTER_MIN_MAG_MIP_POINT)};
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC description{};
    description.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    description.Desc_1_1 = {root_parameter_count, parameters.data(), static_cast<UINT>(std::size(samplers)), samplers,
        D3D12_ROOT_SIGNATURE_FLAG_NONE};
    Microsoft::WRL::ComPtr<ID3DBlob> serialized;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT result = D3D12SerializeVersionedRootSignature(&description, &serialized, &errors);
    if (FAILED(result) && errors)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Root signature: %s", static_cast<const char*>(errors->GetBufferPointer()));
    }
    if (!check(result, "Serialize root signature") ||
        !check(device_->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
            IID_PPV_ARGS(&root_signature_)), "Create root signature"))
    {
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = shader_descriptor_capacity;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (!check(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&shader_heap_)), "Create shader descriptor heap"))
    {
        return false;
    }
    shader_descriptor_stride_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC null_texture{};
    null_texture.Format = backbuffer_format;
    null_texture.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    null_texture.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    null_texture.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(nullptr, &null_texture, shader_heap_->GetCPUDescriptorHandleForHeapStart());

    D3D12_HEAP_PROPERTIES upload{};
    upload.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = binding_layout::frame_constant_bytes;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (auto& slot : frames_)
    {
        void* mapped = nullptr;
        const D3D12_RANGE no_reads{0, 0};
        if (!check(device_->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &buffer,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&slot.constants)), "Create frame constants") ||
            !check(slot.constants->Map(0, &no_reads, &mapped), "Map frame constants"))
        {
            return false;
        }
        slot.mapped_constants = static_cast<std::byte*>(mapped);
    }
    return check_validation();
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12Renderer::shader_descriptor(UINT index) const
{
    auto handle = shader_heap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(index) * shader_descriptor_stride_;
    return handle;
}

void D3D12Renderer::BufferStates::require(ID3D12Resource* buffer, D3D12_RESOURCE_STATES state)
{
    auto found = std::find_if(states_.begin(), states_.end(), [buffer](const auto& entry) { return entry.first == buffer; });
    if (found == states_.end())
    {
        states_.emplace_back(buffer, D3D12_RESOURCE_STATE_COMMON);
        found = states_.end() - 1;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    if (found->second == state)
    {
        if (state != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) return;
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = buffer;
    }
    else
    {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {buffer, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, found->second, state};
        found->second = state;
    }
    pending_.push_back(barrier);
}

void D3D12Renderer::BufferStates::flush(ID3D12GraphicsCommandList* commands)
{
    if (pending_.empty()) return;
    commands->ResourceBarrier(static_cast<UINT>(pending_.size()), pending_.data());
    pending_.clear();
}
}
