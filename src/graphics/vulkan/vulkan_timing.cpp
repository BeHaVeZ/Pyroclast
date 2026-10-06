#include "vulkan_renderer.hpp"
#include <SDL3/SDL.h>
#include <limits>

namespace pyroclast
{
bool VulkanRenderer::create_timestamps()
{
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_, &count, families.data());
    const auto bits = families[queue_family_].timestampValidBits;
    if (bits == 0)
    {
        SDL_Log("GPU timestamps unavailable on the selected Vulkan queue; rendering remains enabled.");
        return true;
    }
    timestamp_mask_ = bits == 64 ? std::numeric_limits<std::uint64_t>::max() : (std::uint64_t{1} << bits) - 1;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_, &properties);
    timestamp_period_ = properties.limits.timestampPeriod;
    VkQueryPoolCreateInfo queries{};
    queries.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queries.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queries.queryCount = static_cast<std::uint32_t>(frames_.size()) * 4;
    if (!check(vkCreateQueryPool(device_, &queries, nullptr, &timestamps_), "Create timestamp pool")) return false;
    SDL_Log("GPU timestamp measurements enabled (frame-fence protected results).");
    return true;
}

bool VulkanRenderer::read_timestamps(std::size_t frame)
{
    if (!frames_[frame].timestamps_pending) return true;
    std::array<std::uint64_t, 4> values{};
    if (!check(vkGetQueryPoolResults(device_, timestamps_, static_cast<std::uint32_t>(frame) * 4, 4,
        sizeof(values), values.data(), sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT), "Read GPU timestamps")) return false;
    const double scale = timestamp_period_ / 1000000.0;
    gpu_timings_ = GpuTimings{static_cast<double>((values[1] - values[0]) & timestamp_mask_) * scale,
        static_cast<double>((values[3] - values[2]) & timestamp_mask_) * scale, ++timing_sample_id_};
    frames_[frame].timestamps_pending = false;
    return true;
}
}
