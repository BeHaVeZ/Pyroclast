#include "vulkan_renderer.hpp"
#include <SDL3/SDL.h>
#include <limits>

namespace pyroclast
{
bool VulkanRenderer::create_timestamps()
{
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vk_.physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(vk_.physical, &count, families.data());
    const auto bits = families[vk_.queue_family].timestampValidBits;
    if (bits == 0)
    {
        SDL_Log("GPU timestamps unavailable on the selected Vulkan queue; rendering remains enabled.");
        return true;
    }
    timing_.mask = bits == 64 ? std::numeric_limits<std::uint64_t>::max() : (std::uint64_t{1} << bits) - 1;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(vk_.physical, &properties);
    timing_.period = properties.limits.timestampPeriod;
    VkQueryPoolCreateInfo queries{};
    queries.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queries.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queries.queryCount = static_cast<std::uint32_t>(frames_.size()) * queries_per_slot;
    if (!check(vkCreateQueryPool(vk_.device, &queries, nullptr, &timing_.pool), "Create timestamp pool")) return false;
    SDL_Log("GPU timestamp measurements enabled (frame-fence protected results).");
    return true;
}

bool VulkanRenderer::read_timestamps(std::size_t frame)
{
    auto& slot = frames_[frame];
    if (!slot.timestamps_pending) return true;
    std::array<std::uint64_t, queries_per_slot> values{};
    const auto count = 4 + 2 * slot.region_count;
    if (!check(vkGetQueryPoolResults(vk_.device, timing_.pool, static_cast<std::uint32_t>(frame) * queries_per_slot, count,
        count * sizeof(std::uint64_t), values.data(), sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT), "Read GPU timestamps")) return false;
    const double scale = timing_.period / 1000000.0;
    GpuTimings timings{static_cast<double>((values[1] - values[0]) & timing_.mask) * scale,
        static_cast<double>((values[3] - values[2]) & timing_.mask) * scale, ++timing_sample_id_};
    for (std::uint32_t region = 0; region < slot.region_count; ++region)
    {
        const auto* pair = values.data() + 4 + 2 * region;
        timings.regions[region] = {slot.region_labels[region], static_cast<double>((pair[1] - pair[0]) & timing_.mask) * scale};
    }
    timings.region_count = slot.region_count;
    gpu_timings_ = timings;
    slot.timestamps_pending = false;
    return true;
}

std::uint32_t VulkanRenderer::begin_region(VkCommandBuffer command, const char* label, const char* fallback)
{
    auto& slot = frames_[frame_index_];
    if (!regions_ || !timing_.pool || slot.region_count == max_gpu_regions) return no_region;
    const auto region = slot.region_count++;
    slot.region_labels[region] = label ? label : fallback;
    vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timing_.pool,
        static_cast<std::uint32_t>(frame_index_) * queries_per_slot + 4 + 2 * region);
    memory_barrier(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);
    return region;
}

void VulkanRenderer::end_region(VkCommandBuffer command, std::uint32_t region)
{
    if (region == no_region) return;
    vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timing_.pool,
        static_cast<std::uint32_t>(frame_index_) * queries_per_slot + 5 + 2 * region);
}
}
