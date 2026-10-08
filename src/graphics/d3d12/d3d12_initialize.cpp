#include "d3d12_renderer.hpp"
#include "../adapter_name.hpp"

#include <SDL3/SDL.h>

namespace pyroclast
{
using Microsoft::WRL::ComPtr;

bool D3D12Renderer::create_device([[maybe_unused]] bool gpu_validation, std::string_view requested_adapter)
{
    UINT factory_flags = 0;
#ifndef NDEBUG
    ComPtr<ID3D12Debug> debug;
    if (!check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),
        "Enable D3D12 debug layer (install Windows Graphics Tools if unavailable)"))
    {
        return false;
    }
    debug->EnableDebugLayer();
    factory_flags = DXGI_CREATE_FACTORY_DEBUG;
    SDL_Log("D3D12 debug layer enabled.");
    if (gpu_validation)
    {
        ComPtr<ID3D12Debug1> gpu_debug;
        if (!check(debug.As(&gpu_debug), "Query D3D12 GPU-based validation"))
        {
            return false;
        }
        gpu_debug->SetEnableGPUBasedValidation(TRUE);
        SDL_Log("D3D12 GPU-based validation enabled.");
    }
#endif

    if (!check(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory_)), "Create DXGI factory"))
    {
        return false;
    }

    const bool warp = is_warp_request(requested_adapter);
    for (UINT index = 0; ; ++index)
    {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT result = warp ? (index == 0 ? factory_->EnumWarpAdapter(IID_PPV_ARGS(&adapter)) : DXGI_ERROR_NOT_FOUND) :
            factory_->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
        if (result == DXGI_ERROR_NOT_FOUND)
        {
            if (requested_adapter.empty()) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No hardware adapter supports D3D12 feature level 11.0.");
            else SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No D3D12 feature level 11.0 adapter name contains '%.*s'.",
                static_cast<int>(requested_adapter.size()), requested_adapter.data());
            return false;
        }
        if (!check(result, "Enumerate graphics adapter"))
        {
            return false;
        }

        DXGI_ADAPTER_DESC1 description{};
        if (!check(adapter->GetDesc1(&description), "Read adapter description"))
        {
            return false;
        }
        char adapter_name[512]{};
        WideCharToMultiByte(CP_UTF8, 0, description.Description, -1,
            adapter_name, static_cast<int>(sizeof(adapter_name)), nullptr, nullptr);
        const bool wanted = warp || (requested_adapter.empty() ? (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 :
            adapter_matches(adapter_name, requested_adapter));
        if (!wanted || FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
        {
            continue;
        }
        if (!check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&device_)), "Create D3D12 device"))
        {
            return false;
        }

        adapter_name_ = adapter_name;
        if (FAILED(adapter.As(&adapter_))) adapter_.Reset();
        SDL_Log("D3D12 adapter: %s (video memory query %s)", adapter_name, adapter_ ? "available" : "unavailable");
        break;
    }

#ifndef NDEBUG
    if (!check(device_.As(&info_queue_), "Create D3D12 validation queue"))
    {
        return false;
    }
    if (IsDebuggerPresent())
    {
        if (!check(info_queue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE),
                "Enable validation break on corruption") ||
            !check(info_queue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE),
                "Enable validation break on error"))
        {
            return false;
        }
    }
#endif

    D3D12_COMMAND_QUEUE_DESC queue_description{};
    queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (!check(device_->CreateCommandQueue(&queue_description, IID_PPV_ARGS(&queue_)),
            "Create direct command queue") ||
        !check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)),
            "Create GPU fence"))
    {
        return false;
    }
    fence_event_.Attach(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!fence_event_.IsValid())
    {
        return check(HRESULT_FROM_WIN32(GetLastError()), "Create fence event");
    }

    for (auto& frame : frames_)
    {
        if (!check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&frame.allocator)), "Create frame command allocator"))
        {
            return false;
        }
    }
    if (!check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            frames_[0].allocator.Get(), nullptr, IID_PPV_ARGS(&command_list_)),
            "Create command list") ||
        !check(command_list_->Close(), "Close initial command list"))
    {
        return false;
    }
    return true;
}

bool D3D12Renderer::create_swapchain(SDL_Window* window)
{
    const auto properties = SDL_GetWindowProperties(window);
    const auto hwnd = static_cast<HWND>(SDL_GetPointerProperty(properties,
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd || !SDL_GetWindowSizeInPixels(window, &width_, &height_) ||
        width_ <= 0 || height_ <= 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "D3D12 requires a Win32 window with a nonzero pixel size: %s", SDL_GetError());
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = static_cast<UINT>(width_);
    description.Height = static_cast<UINT>(height_);
    description.Format = backbuffer_format;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = backbuffer_count;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> swapchain;
    if (!check(factory_->CreateSwapChainForHwnd(queue_.Get(), hwnd, &description,
            nullptr, nullptr, &swapchain), "Create swapchain") ||
        !check(swapchain.As(&swapchain_), "Query swapchain interface") ||
        !check(factory_->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER),
            "Disable implicit fullscreen switching"))
    {
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC heap_description{};
    heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_description.NumDescriptors = backbuffer_count;
    if (!check(device_->CreateDescriptorHeap(&heap_description, IID_PPV_ARGS(&rtv_heap_)),
        "Create render target descriptor heap"))
    {
        return false;
    }
    rtv_stride_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    dsv_stride_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    return create_render_targets();
}

bool D3D12Renderer::create_render_targets()
{
    auto handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    for (UINT index = 0; index < backbuffer_count; ++index)
    {
        if (!check(swapchain_->GetBuffer(index, IID_PPV_ARGS(&backbuffers_[index])),
            "Acquire swapchain backbuffer"))
        {
            return false;
        }
        device_->CreateRenderTargetView(backbuffers_[index].Get(), nullptr, handle);
        handle.ptr += rtv_stride_;
    }
    return check_validation();
}

bool D3D12Renderer::initialize(SDL_Window* window, const GraphicsDeviceOptions& options)
{
    vsync_ = options.vsync;
    capture_enabled_ = options.capture;
    if (!create_device(options.gpu_validation, options.adapter) || !create_bindings() || !create_swapchain(window) || !check_validation())
    {
        return false;
    }
    regions_ = options.gpu_regions;
    if ((options.gpu_timing || options.gpu_regions) && !create_timestamps()) return false;
    if (options.debug_overlay && !create_overlay()) return false;
    SDL_Log("D3D12 initialized: %dx%d, two backbuffers, VSync %s; presentation: %.*s.", width_, height_, vsync_ ? "enabled" : "disabled",
        static_cast<int>(present_info().mode.size()), present_info().mode.data());
    return true;
}
}
