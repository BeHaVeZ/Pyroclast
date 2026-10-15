#pragma once

#include "../backend_common.hpp"
#include "vulkan_handle.hpp"

#include <vulkan/vulkan.h>

#include <atomic>
#include <optional>
#include <vector>

namespace pyroclast
{
struct VulkanContext
{
    VulkanContext() = default;
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    ~VulkanContext();
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0;
    bool surface_maintenance = false;
    bool present_fences = false;
    bool memory_budget = false;
    PFN_vkCmdBeginDebugUtilsLabelEXT begin_label = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT end_label = nullptr;
    std::atomic_uint64_t validation_warnings{0};
    std::atomic_uint64_t validation_errors{0};
};

struct VulkanFrameSlot
{
    bool timestamps_pending = false;
    TimestampRegions regions;
    UniqueCommandPool pool;
    VkCommandBuffer command = VK_NULL_HANDLE;
    UniqueSemaphore acquired;
    UniqueFence submitted;
    UniqueMemory constants_memory;
    UniqueBuffer constants;
    std::byte* mapped_constants = nullptr;
    UniqueMemory readback_memory;
    UniqueBuffer readback;
    const std::byte* mapped_readback = nullptr;
    std::uint32_t readback_size = 0;
    bool readback_pending = false;
};

struct VulkanSwapchain
{
    struct Image
    {
        VkImage image = VK_NULL_HANDLE;
        UniqueImageView view;
        UniqueSemaphore rendered;
        UniqueFence presented;
        bool present_pending = false;
    };
    // Callers wait for the device and presentation to go idle first.
    void reset()
    {
        images.clear();
        handle.reset();
        extent = {};
    }
    UniqueSwapchain handle;
    VkExtent2D extent{};
    std::vector<Image> images;
};

struct VulkanBindings
{
    std::array<UniqueSampler, binding_layout::sampler_count> samplers;
    UniqueDescriptorSetLayout set_layout;
    UniquePipelineLayout layout;
    PFN_vkCmdPushDescriptorSetKHR push = nullptr;
};

struct VulkanTiming
{
    UniqueQueryPool pool;
    std::uint64_t mask = 0;
    double period = 0;
};

struct VulkanCapture
{
    UniqueMemory memory;
    UniqueBuffer buffer;
    VkDeviceSize size = 0;
    VkExtent2D extent{};
    bool pending = false;
};

void transition_image(VkCommandBuffer command, VkImage image, VkImageLayout from, VkImageLayout to,
    VkPipelineStageFlags2 source_stage, VkAccessFlags2 source_access,
    VkPipelineStageFlags2 destination_stage, VkAccessFlags2 destination_access,
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);
void transition_buffer(VkCommandBuffer command, VkBuffer buffer,
    VkPipelineStageFlags2 source_stage, VkAccessFlags2 source_access,
    VkPipelineStageFlags2 destination_stage, VkAccessFlags2 destination_access);
void memory_barrier(VkCommandBuffer command,
    VkPipelineStageFlags2 source_stage, VkAccessFlags2 source_access,
    VkPipelineStageFlags2 destination_stage, VkAccessFlags2 destination_access);

std::optional<std::uint32_t> find_memory_type(VkPhysicalDevice physical, std::uint32_t type_bits,
    VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred);

class VulkanBuffer;

class VulkanRenderer final : public GraphicsDevice
{
public:
    VulkanRenderer() = default;
    ~VulkanRenderer() override;
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    bool initialize(SDL_Window* window, const GraphicsDeviceOptions& options);
    std::unique_ptr<Buffer> create_buffer(std::uint32_t size, std::span<const std::byte> initial) override;
    std::unique_ptr<GraphicsPipeline> create_graphics_pipeline(
        const GraphicsPipelineDescription& description) override;
    bool resize(int width, int height) override;
    std::unique_ptr<ComputePipeline> create_compute_pipeline(Shader shader) override;
    std::unique_ptr<RenderTarget> create_render_target(const RenderTargetDescription& description) override;
    RenderResult render(const Frame& frame) override;
    bool wait_idle() override;
    std::optional<GpuTimings> completed_gpu_timings() const override { return gpu_timings_; }
    std::optional<ReadbackData> completed_readback() const override { return readback_; }
    std::optional<CapturedImage> read_capture() override;
    std::string_view adapter_name() const override { return adapter_name_; }
    SubmitTimings last_submit_timings() const override { return submit_timings_; }
    PresentInfo present_info() const override { return {present_mode_, static_cast<std::uint32_t>(swapchain_.images.size())}; }
    std::optional<GpuMemory> memory_usage() const override;
    std::uint64_t swapchain_rebuilds() const override { return swapchain_rebuilds_; }

private:
    static constexpr VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
    VkFormat vulkan_format(TargetFormat format) const
    {
        return format == TargetFormat::depth ? depth_format : format == TargetFormat::rgba16_float ? VK_FORMAT_R16G16B16A16_SFLOAT :
            format == TargetFormat::r11g11b10_float ? VK_FORMAT_B10G11R11_UFLOAT_PACK32 : format_;
    }
    bool create_instance(bool gpu_validation);
    bool create_device(std::string_view requested_adapter);
    bool create_bindings();
    bool create_shader_module(Shader shader, struct VulkanShaderModule& module);
    bool create_timestamps();
    std::uint32_t begin_region(VkCommandBuffer command, const char* label, const char* fallback);
    void end_region(VkCommandBuffer command, std::uint32_t region);
    bool read_timestamps(std::size_t frame);
    void read_readback(VulkanFrameSlot& slot);
    bool create_frames();
    bool create_swapchain();
    bool create_capture_buffer(VkDeviceSize size);
    bool prepare_capture();
    void record_capture(VkCommandBuffer command, VkImage image);
    bool create_overlay();
    void destroy_overlay();
    void record_overlay(VkCommandBuffer command, ImDrawData* overlay, VkImageView backbuffer, VkExtent2D extent);
    bool size_target(class VulkanRenderTarget& target);
    void push_bindings(VkCommandBuffer command, VkPipelineBindPoint point, const Bindings& bindings,
        const VulkanFrameSlot& slot, std::span<const RenderTarget* const> inputs);
    void set_viewport(VkCommandBuffer command, VkExtent2D extent);
    void begin_label(VkCommandBuffer command, const char* label) const;
    void end_label(VkCommandBuffer command, const char* label) const;
    // Records commands into a one-off command buffer and waits for them; startup and resize only.
    template <typename Record> bool submit_immediately(const char* operation, Record&& record);
    bool allocate_buffer(VulkanBuffer& buffer, VkBufferUsageFlags usage,
        VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred, const char* purpose);
    bool allocate_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags required,
        VkMemoryPropertyFlags preferred, const char* purpose, UniqueBuffer& buffer, UniqueMemory& memory);
    bool check(VkResult result, const char* operation);
    bool validation_ok() const;
    static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* data, void* user);

    VulkanContext vk_;
    VulkanBindings bindings_;
    VulkanTiming timing_;
    std::array<VulkanFrameSlot, 2> frames_{};
    VulkanSwapchain swapchain_;
    VulkanCapture capture_;
    SDL_Window* window_ = nullptr;
    std::string adapter_name_;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    std::size_t frame_index_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool rebuild_ = false;
    bool vsync_ = true;
    bool capture_enabled_ = false;
    bool overlay_ = false;
    bool regions_ = false;
    std::optional<GpuTimings> gpu_timings_;
    std::uint64_t timing_sample_id_ = 0;
    SubmitTimings submit_timings_;
    std::uint64_t swapchain_rebuilds_ = 0;
    std::string_view present_mode_ = "fifo";
    std::optional<ReadbackData> readback_;
    std::uint64_t readback_sample_id_ = 0;
};
}
