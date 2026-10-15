#include "d3d12_renderer.hpp"
#include "d3d12_resources.hpp"

#include <SDL3/SDL.h>

namespace pyroclast
{
std::unique_ptr<RenderTarget> D3D12Renderer::create_render_target(const RenderTargetDescription& description)
{
    if (description.divisor == 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Render target divisor must be at least one.");
        return nullptr;
    }
    if (next_shader_descriptor_ == overlay_descriptor_first)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "The render target range of the shader descriptor heap is full.");
        return nullptr;
    }
    auto target = std::make_unique<D3D12RenderTarget>(description, this);
    target->shader_view = next_shader_descriptor_++;
    const bool depth = description.format == TargetFormat::depth;
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.NumDescriptors = depth ? 2 : 1;
    heap.Type = depth ? D3D12_DESCRIPTOR_HEAP_TYPE_DSV : D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (!check(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&target->render_view)), "Create render target view heap")) return nullptr;
    return check_validation() ? std::move(target) : nullptr;
}

bool D3D12Renderer::size_target(D3D12RenderTarget& target)
{
    const auto extent = target.extent_for(static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_));
    const int width = static_cast<int>(extent[0]);
    const int height = static_cast<int>(extent[1]);
    if (target.texture && target.width == width && target.height == height)
    {
        return true;
    }
    if (!wait_idle())
    {
        return false;
    }
    const bool depth = target.description().format == TargetFormat::depth;
    D3D12_RESOURCE_DESC texture{};
    texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture.Width = static_cast<UINT64>(width);
    texture.Height = static_cast<UINT>(height);
    texture.DepthOrArraySize = 1;
    texture.MipLevels = 1;
    const auto format = target.description().format;
    texture.Format = depth ? DXGI_FORMAT_R32_TYPELESS : color_format(format);
    texture.SampleDesc.Count = 1;
    texture.Flags = depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    const auto& color = target.description().clear_color;
    D3D12_CLEAR_VALUE clear{color_format(format), {color[0], color[1], color[2], color[3]}};
    if (depth)
    {
        clear.Format = depth_format;
        clear.DepthStencil = {target.description().clear_depth, 0};
    }
    if (!check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture,
        depth ? depth_resting_state : color_resting_state, &clear, IID_PPV_ARGS(&target.texture)), "Create render target")) return false;
    auto render_view = target.render_view->GetCPUDescriptorHandleForHeapStart();
    if (depth)
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC depth_view{};
        depth_view.Format = depth_format;
        depth_view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device_->CreateDepthStencilView(target.texture.Get(), &depth_view, render_view);
        render_view.ptr += dsv_stride_;
        depth_view.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
        device_->CreateDepthStencilView(target.texture.Get(), &depth_view, render_view);
    }
    else
    {
        device_->CreateRenderTargetView(target.texture.Get(), nullptr, render_view);
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = depth ? DXGI_FORMAT_R32_FLOAT : color_format(format);
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1;
    auto descriptor = shader_heap_->GetCPUDescriptorHandleForHeapStart();
    descriptor.ptr += static_cast<SIZE_T>(target.shader_view) * shader_descriptor_stride_;
    device_->CreateShaderResourceView(target.texture.Get(), &view, descriptor);
    target.width = width;
    target.height = height;
    SDL_Log("D3D12 render target: %dx%d, %s.", width, height, depth ? "D32" : format == TargetFormat::rgba16_float ? "RGBA16F" :
        format == TargetFormat::r11g11b10_float ? "R11G11B10F" : "RGBA8");
    return check_validation();
}
}
