#include "vulkan_renderer.hpp"
#include "../adapter_name.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstring>

namespace pyroclast
{
VKAPI_ATTR VkBool32 VKAPI_CALL VulkanRenderer::debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* user)
{
    auto& renderer = *static_cast<VulkanRenderer*>(user);
    const bool error = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;
    const bool validation = (type & (VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)) != 0;
    if (validation)
    {
        ++(error ? renderer.vk_.validation_errors : renderer.vk_.validation_warnings);
    }
    const char* source = validation ? "Vulkan validation" : "Vulkan loader";
    if (error) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", source, data->pMessage);
    else SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", source, data->pMessage);
    return VK_FALSE;
}

bool VulkanRenderer::check(VkResult result, const char* operation)
{
    if (result == VK_SUCCESS)
    {
        return true;
    }
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s failed: VkResult %d%s", operation,
        static_cast<int>(result), result == VK_ERROR_DEVICE_LOST ? " (device lost)" : "");
    return false;
}

bool VulkanRenderer::validation_ok() const
{
    return vk_.validation_errors.load() == 0;
}

bool VulkanRenderer::initialize(SDL_Window* window, const GraphicsDeviceOptions& options)
{
    vsync_ = options.vsync;
    capture_enabled_ = options.capture;
    window_ = window;
    if (!create_instance(options.gpu_validation))
    {
        return false;
    }
    if (!SDL_Vulkan_CreateSurface(window_, vk_.instance, nullptr, &vk_.surface))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Create Vulkan surface failed: %s", SDL_GetError());
        return false;
    }
    if (!create_device(options.adapter) || !create_frames() || !create_bindings() ||
        !SDL_GetWindowSizeInPixels(window_, &width_, &height_) || !create_swapchain())
    {
        return false;
    }
    regions_ = options.gpu_regions;
    if ((options.gpu_timing || options.gpu_regions) && !create_timestamps()) return false;
    if (options.debug_overlay && !create_overlay()) return false;
    SDL_Log("Vulkan initialized: %ux%u, two frames in flight, VSync %s.",
        swapchain_.extent.width, swapchain_.extent.height, vsync_ ? "enabled" : "disabled");
    return validation_ok();
}

// GPU-assisted validation replaces synchronization validation: both together are prohibitively slow.
bool VulkanRenderer::create_instance([[maybe_unused]] bool gpu_validation)
{
    std::uint32_t count = 0;
    const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!sdl_extensions)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Read SDL Vulkan extensions failed: %s", SDL_GetError());
        return false;
    }
    std::vector<const char*> extensions(sdl_extensions, sdl_extensions + count);
    if (!check(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr), "Enumerate Vulkan instance extensions"))
    {
        return false;
    }
    std::vector<VkExtensionProperties> available_extensions(count);
    if (!check(vkEnumerateInstanceExtensionProperties(nullptr, &count, available_extensions.data()), "Read Vulkan instance extensions"))
    {
        return false;
    }
    const auto available = [&available_extensions](const char* name) {
        return std::any_of(available_extensions.begin(), available_extensions.end(),
            [name](const auto& extension) { return std::strcmp(extension.extensionName, name) == 0; });
    };
    vk_.surface_maintenance = available(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME) && available(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
    if (vk_.surface_maintenance)
    {
        extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
        extensions.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
    }
    const bool debug_utils = available(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (debug_utils) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    std::vector<const char*> layers;
    VkDebugUtilsMessengerCreateInfoEXT debug{};
    debug.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = debug_callback;
    debug.pUserData = this;
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
#ifndef NDEBUG
    if (!check(vkEnumerateInstanceLayerProperties(&count, nullptr), "Enumerate Vulkan layers"))
    {
        return false;
    }
    std::vector<VkLayerProperties> available_layers(count);
    if (!check(vkEnumerateInstanceLayerProperties(&count, available_layers.data()), "Read Vulkan layers"))
    {
        return false;
    }
    const bool validation_available = std::any_of(available_layers.begin(), available_layers.end(),
        [](const auto& layer) { return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0; });
    if (!validation_available)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Vulkan Debug requires VK_LAYER_KHRONOS_validation. Install the Vulkan SDK, or set VK_ADD_LAYER_PATH to its Bin directory.");
        return false;
    }
    layers.push_back("VK_LAYER_KHRONOS_validation");
    if (!debug_utils)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Vulkan Debug requires VK_EXT_debug_utils for validation messages.");
        return false;
    }
    extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
    const VkValidationFeatureEnableEXT enabled = gpu_validation ? VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT :
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validation{};
    validation.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
    validation.enabledValidationFeatureCount = 1;
    validation.pEnabledValidationFeatures = &enabled;
    debug.pNext = &validation;
    info.pNext = &debug;
#endif
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "Pyroclast";
    application.apiVersion = VK_API_VERSION_1_3;
    info.pApplicationInfo = &application;
    info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    if (!check(vkCreateInstance(&info, nullptr, &vk_.instance), "Create Vulkan 1.3 instance"))
    {
        return false;
    }
    if (debug_utils)
    {
        vk_.begin_label = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(vkGetInstanceProcAddr(vk_.instance, "vkCmdBeginDebugUtilsLabelEXT"));
        vk_.end_label = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetInstanceProcAddr(vk_.instance, "vkCmdEndDebugUtilsLabelEXT"));
    }
#ifndef NDEBUG
    debug.pNext = nullptr;
    const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(vk_.instance, "vkCreateDebugUtilsMessengerEXT"));
    if (!create || !check(create(vk_.instance, &debug, nullptr, &vk_.messenger), "Create Vulkan debug messenger"))
    {
        return false;
    }
    SDL_Log("Vulkan validation and %s validation enabled.", gpu_validation ? "GPU-assisted" : "synchronization");
#endif
    return validation_ok();
}

// Adapters must offer D3D12's compute limits (1,024 threads, 32 KB shared memory), which the shared HLSL may use,
// and a uniform range that holds the frame constants (Vulkan guarantees only 16 KB).
bool VulkanRenderer::create_device(std::string_view requested_adapter)
{
    if (is_warp_request(requested_adapter))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WARP is a D3D12 adapter; choose a Vulkan device by name.");
        return false;
    }
    std::uint32_t count = 0;
    if (!check(vkEnumeratePhysicalDevices(vk_.instance, &count, nullptr), "Enumerate Vulkan adapters"))
    {
        return false;
    }
    std::vector<VkPhysicalDevice> adapters(count);
    if (!check(vkEnumeratePhysicalDevices(vk_.instance, &count, adapters.data()), "Read Vulkan adapters"))
    {
        return false;
    }
    int best_score = 0;
    bool best_present_fences = false;
    bool best_memory_budget = false;
    bool name_matched = requested_adapter.empty();
    for (const auto adapter : adapters)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(adapter, &properties);
        const auto skip = [&properties](const char* reason, const char* detail = "") {
            SDL_Log("Vulkan adapter %s not used: %s%s.", properties.deviceName, reason, detail);
        };
        if (!requested_adapter.empty() && !adapter_matches(properties.deviceName, requested_adapter))
        {
            skip("name does not match --adapter");
            continue;
        }
        name_matched = true;
        if (properties.apiVersion < VK_API_VERSION_1_3 || properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
        {
            skip(properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ? "software device" : "Vulkan 1.3 is not supported");
            continue;
        }
        if (!check(vkEnumerateDeviceExtensionProperties(adapter, nullptr, &count, nullptr), "Enumerate Vulkan device extensions"))
        {
            return false;
        }
        std::vector<VkExtensionProperties> extensions(count);
        if (!check(vkEnumerateDeviceExtensionProperties(adapter, nullptr, &count, extensions.data()), "Read Vulkan device extensions"))
        {
            return false;
        }
        const auto has_extension = [&extensions](const char* name) {
            return std::any_of(extensions.begin(), extensions.end(), [name](const auto& extension) {
                return std::strcmp(extension.extensionName, name) == 0;
            });
        };
        const char* missing = nullptr;
        for (const char* required : {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME})
        {
            if (!missing && !has_extension(required)) missing = required;
        }
        if (missing)
        {
            skip("missing ", missing);
            continue;
        }
        const bool has_maintenance = vk_.surface_maintenance && has_extension(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
        VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance{};
        maintenance.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
        VkPhysicalDeviceVulkan13Features vulkan13{};
        vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        vulkan13.pNext = has_maintenance ? &maintenance : nullptr;
        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &vulkan13;
        vkGetPhysicalDeviceFeatures2(adapter, &features);
        if (!vulkan13.dynamicRendering || !vulkan13.synchronization2)
        {
            skip("missing feature ", !vulkan13.dynamicRendering ? "dynamicRendering" : "synchronization2");
            continue;
        }
        const auto& limits = properties.limits;
        if (limits.maxComputeWorkGroupInvocations < 1024 || limits.maxComputeWorkGroupSize[0] < 1024 ||
            limits.maxComputeSharedMemorySize < 32768)
        {
            skip("compute groups smaller than D3D12's (1,024 threads, 32 KB shared memory)");
            continue;
        }
        if (limits.maxUniformBufferRange < binding_layout::frame_constant_bytes)
        {
            skip("a uniform buffer range below the frame constants' size");
            continue;
        }
        VkFormatProperties depth_properties{};
        vkGetPhysicalDeviceFormatProperties(adapter, depth_format, &depth_properties);
        constexpr VkFormatFeatureFlags depth_features = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((depth_properties.optimalTilingFeatures & depth_features) != depth_features)
        {
            skip("D32_SFLOAT cannot be a sampled depth attachment");
            continue;
        }
        const bool present_fences = has_maintenance && maintenance.swapchainMaintenance1;
        const bool memory_budget = has_extension(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
        bool queue_found = false;
        vkGetPhysicalDeviceQueueFamilyProperties(adapter, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(adapter, &count, families.data());
        for (std::uint32_t index = 0; index < count; ++index)
        {
            VkBool32 present = VK_FALSE;
            if (!check(vkGetPhysicalDeviceSurfaceSupportKHR(adapter, index, vk_.surface, &present), "Query Vulkan presentation support"))
            {
                return false;
            }
            constexpr VkQueueFlags required_queue = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            if ((families[index].queueFlags & required_queue) != required_queue || !present)
            {
                continue;
            }
            const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 100 : 50;
            if (score > best_score)
            {
                best_score = score;
                best_present_fences = present_fences;
                best_memory_budget = memory_budget;
                vk_.physical = adapter;
                vk_.queue_family = index;
            }
            queue_found = true;
            break;
        }
        if (!queue_found) skip("no graphics and compute queue can present to the window");
    }
    if (!name_matched)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No Vulkan adapter name contains '%.*s'.",
            static_cast<int>(requested_adapter.size()), requested_adapter.data());
        return false;
    }
    if (!vk_.physical)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "No hardware Vulkan 1.3 adapter supports a graphics/compute/present queue, dynamic rendering, synchronization2, "
            "VK_KHR_push_descriptor and sampled D32_SFLOAT depth attachments.");
        return false;
    }
    vk_.present_fences = best_present_fences;
    vk_.memory_budget = best_memory_budget;
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{};
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex = vk_.queue_family;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance{};
    maintenance.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
    maintenance.swapchainMaintenance1 = VK_TRUE;
    VkPhysicalDeviceVulkan13Features vulkan13{};
    vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13.pNext = vk_.present_fences ? &maintenance : nullptr;
    vulkan13.dynamicRendering = VK_TRUE;
    vulkan13.synchronization2 = VK_TRUE;
    std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME};
    if (vk_.present_fences) extensions.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
    if (vk_.memory_budget) extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.pNext = &vulkan13;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue;
    info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    if (!check(vkCreateDevice(vk_.physical, &info, nullptr, &vk_.device), "Create Vulkan device"))
    {
        return false;
    }
    vkGetDeviceQueue(vk_.device, vk_.queue_family, 0, &vk_.queue);
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(vk_.physical, &properties);
    adapter_name_ = properties.deviceName;
    SDL_Log("Vulkan adapter: %s (present fences %s; memory budget %s).", properties.deviceName,
        vk_.present_fences ? "available" : "unavailable; resize and shutdown wait for device idle",
        vk_.memory_budget ? "available" : "unavailable");
    return validation_ok();
}

bool VulkanRenderer::create_frames()
{
    for (auto& frame : frames_)
    {
        VkCommandPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.queueFamilyIndex = vk_.queue_family;
        VkCommandBufferAllocateInfo command{};
        command.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command.commandBufferCount = 1;
        VkSemaphoreCreateInfo semaphore{};
        semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fence{};
        fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (!check(vkCreateCommandPool(vk_.device, &pool, nullptr, frame.pool.put(vk_.device)), "Create frame command pool"))
        {
            return false;
        }
        command.commandPool = frame.pool;
        if (!check(vkAllocateCommandBuffers(vk_.device, &command, &frame.command), "Allocate frame command buffer") ||
            !check(vkCreateSemaphore(vk_.device, &semaphore, nullptr, frame.acquired.put(vk_.device)), "Create image-acquired semaphore") ||
            !check(vkCreateFence(vk_.device, &fence, nullptr, frame.submitted.put(vk_.device)), "Create frame fence"))
        {
            return false;
        }
    }
    return validation_ok();
}
}
