#pragma once

#include "../graphics_device.hpp"

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
    std::atomic_uint64_t validation_warnings{0};
    std::atomic_uint64_t validation_errors{0};
};

struct VulkanFrameSlot
{
    VulkanFrameSlot() = default;
    VulkanFrameSlot(const VulkanFrameSlot&) = delete;
    VulkanFrameSlot& operator=(const VulkanFrameSlot&) = delete;
    ~VulkanFrameSlot();
    VkDevice device = VK_NULL_HANDLE;
    bool timestamps_pending = false;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkFence submitted = VK_NULL_HANDLE;
    VkBuffer constants = VK_NULL_HANDLE;
    VkDeviceMemory constants_memory = VK_NULL_HANDLE;
    std::byte* mapped_constants = nullptr;
};

struct VulkanSwapchain
{
    struct Image
    {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSemaphore rendered = VK_NULL_HANDLE;
        VkFence presented = VK_NULL_HANDLE;
        bool present_pending = false;
    };
    VulkanSwapchain() = default;
    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;
    ~VulkanSwapchain() { reset(); }
    // Callers wait for the device and presentation to go idle first.
    void reset();
    VkDevice device = VK_NULL_HANDLE;
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    VkExtent2D extent{};
    std::vector<Image> images;
};

struct VulkanBindings
{
    VulkanBindings() = default;
    VulkanBindings(const VulkanBindings&) = delete;
    VulkanBindings& operator=(const VulkanBindings&) = delete;
    ~VulkanBindings();
    VkDevice device = VK_NULL_HANDLE;
    std::array<VkSampler, binding_layout::sampler_count> samplers{};
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    PFN_vkCmdPushDescriptorSetKHR push = nullptr;
};

struct VulkanTiming
{
    VulkanTiming() = default;
    VulkanTiming(const VulkanTiming&) = delete;
    VulkanTiming& operator=(const VulkanTiming&) = delete;
    ~VulkanTiming();
    VkDevice device = VK_NULL_HANDLE;
    VkQueryPool pool = VK_NULL_HANDLE;
    std::uint64_t mask = 0;
    double period = 0;
};

struct VulkanCapture
{
    VulkanCapture() = default;
    VulkanCapture(const VulkanCapture&) = delete;
    VulkanCapture& operator=(const VulkanCapture&) = delete;
    ~VulkanCapture();
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    VkExtent2D extent{};
    bool pending = false;
};

void transition_image(VkCommandBuffer command, VkImage image, VkImageLayout from, VkImageLayout to,
    VkPipelineStageFlags2 source_stage, VkAccessFlags2 source_access,
    VkPipelineStageFlags2 destination_stage, VkAccessFlags2 destination_access);
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
    std::optional<CapturedImage> read_capture() override;
    std::string_view adapter_name() const override { return adapter_name_; }

private:
    bool create_instance();
    bool create_device();
    bool create_bindings();
    bool create_shader_module(Shader shader, struct VulkanShaderModule& module);
    bool create_timestamps();
    bool read_timestamps(std::size_t frame);
    bool create_frames();
    bool create_swapchain();
    bool create_capture_buffer(VkDeviceSize size);
    bool record_capture(VkCommandBuffer command, VkImage image);
    bool size_target(class VulkanRenderTarget& target);
    bool owns(const Frame& frame) const;
    void push_bindings(VkCommandBuffer command, VkPipelineBindPoint point, const Bindings& bindings,
        const VulkanFrameSlot& slot, std::span<const RenderTarget* const> inputs);
    void set_viewport(VkCommandBuffer command, VkExtent2D extent);
    // Records commands into a one-off command buffer and waits for them; startup and resize only.
    template <typename Record> bool submit_immediately(const char* operation, Record&& record);
    bool allocate_buffer(VulkanBuffer& buffer, VkBufferUsageFlags usage,
        VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred, const char* purpose);
    bool allocate_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags required,
        VkMemoryPropertyFlags preferred, const char* purpose, VkBuffer& buffer, VkDeviceMemory& memory);
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
    std::optional<GpuTimings> gpu_timings_;
    std::uint64_t timing_sample_id_ = 0;
};
}
