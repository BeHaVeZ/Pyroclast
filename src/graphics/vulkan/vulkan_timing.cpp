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
    queries.queryCount = static_cast<std::uint32_t>(frames_.size()) * timestamp_queries_per_slot;
    if (!check(vkCreateQueryPool(vk_.device, &queries, nullptr, timing_.pool.put(vk_.device)), "Create timestamp pool")) return false;
    SDL_Log("GPU timestamp measurements enabled (frame-fence protected results).");
    return true;
}

bool VulkanRenderer::read_timestamps(std::size_t frame)
{
    auto& slot = frames_[frame];
    if (!slot.timestamps_pending) return true;
    std::array<std::uint64_t, timestamp_queries_per_slot> values{};
    const auto count = 4 + 2 * slot.regions.count;
    if (!check(vkGetQueryPoolResults(vk_.device, timing_.pool, static_cast<std::uint32_t>(frame) * timestamp_queries_per_slot, count,
        count * sizeof(std::uint64_t), values.data(), sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT), "Read GPU timestamps")) return false;
    gpu_timings_ = gpu_timings(values, timing_.mask, timing_.period / 1000000.0, slot.regions, ++timing_sample_id_);
    slot.timestamps_pending = false;
    return true;
}

std::uint32_t VulkanRenderer::begin_region(VkCommandBuffer command, const char* label, const char* fallback)
{
    if (!regions_ || !timing_.pool) return no_region;
    const auto region = frames_[frame_index_].regions.begin(label, fallback);
    if (region == no_region) return no_region;
    vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timing_.pool,
        static_cast<std::uint32_t>(frame_index_) * timestamp_queries_per_slot + TimestampRegions::begin_query(region));
    memory_barrier(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE);
    return region;
}

void VulkanRenderer::end_region(VkCommandBuffer command, std::uint32_t region)
{
    if (region == no_region) return;
    vkCmdWriteTimestamp2(command, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, timing_.pool,
        static_cast<std::uint32_t>(frame_index_) * timestamp_queries_per_slot + TimestampRegions::end_query(region));
}
}
