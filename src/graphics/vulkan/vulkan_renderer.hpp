#pragma once

#include "../graphics_device.hpp"

#include <vulkan/vulkan.h>

#include <atomic>
#include <vector>

namespace pyroclast
{
class VulkanRenderer final : public GraphicsDevice
{
public:
    VulkanRenderer() = default;
    ~VulkanRenderer() override;
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    bool initialize(SDL_Window* window, const GraphicsDeviceOptions& options);
    std::unique_ptr<VertexBuffer> create_vertex_buffer(std::span<const std::byte> vertices, std::uint32_t stride) override;
    std::unique_ptr<GraphicsPipeline> create_graphics_pipeline(
        const GraphicsPipelineDescription& description) override;
    bool resize(int width, int height) override;
    std::unique_ptr<ComputePipeline> create_compute_pipeline(
        Shader shader, const VertexBuffer& vertices) override;
    RenderResult render(const DrawCommand& draw) override;
    bool wait_idle() override;
    std::optional<GpuTimings> completed_gpu_timings() const override { return gpu_timings_; }

private:
    struct Frame
    {
        bool timestamps_pending = false;
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkSemaphore acquired = VK_NULL_HANDLE;
        VkFence submitted = VK_NULL_HANDLE;
    };
    struct Image
    {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkSemaphore rendered = VK_NULL_HANDLE;
        VkFence presented = VK_NULL_HANDLE;
        bool present_pending = false;
    };

    bool create_instance();
    bool create_device();
    bool create_shader_module(Shader shader, struct VulkanShaderModule& module);
    bool create_timestamps();
    bool read_timestamps(std::size_t frame);
    bool create_frames();
    bool create_swapchain();
    void destroy_swapchain();
    bool create_smoke_pipeline();
    bool create_smoke_target();
    void destroy_smoke_target();
    void set_viewport(VkCommandBuffer command, VkExtent2D extent);
    void draw_range(VkCommandBuffer command, const DrawRange& range, const VertexBuffer& vertices, float aspect);
    void render_smoke(VkCommandBuffer command, const DrawCommand& draw);
    bool check(VkResult result, const char* operation);
    bool validation_ok() const;
    static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* data, void* user);

    SDL_Window* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t queue_family_ = 0;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::array<Frame, 2> frames_{};
    std::vector<Image> images_;
    std::size_t frame_index_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool rebuild_ = false;
    bool vsync_ = true;
    bool half_smoke_ = false;
    VkRenderPass smoke_pass_ = VK_NULL_HANDLE;
    VkImage smoke_image_ = VK_NULL_HANDLE;
    VkDeviceMemory smoke_memory_ = VK_NULL_HANDLE;
    VkImageView smoke_view_ = VK_NULL_HANDLE;
    VkFramebuffer smoke_framebuffer_ = VK_NULL_HANDLE;
    VkExtent2D smoke_extent_{};
    VkSampler smoke_sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout smoke_descriptors_ = VK_NULL_HANDLE;
    VkDescriptorPool smoke_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet smoke_set_ = VK_NULL_HANDLE;
    VkPipelineLayout smoke_layout_ = VK_NULL_HANDLE;
    VkPipeline smoke_composite_ = VK_NULL_HANDLE;
    VkQueryPool timestamps_ = VK_NULL_HANDLE;
    std::uint64_t timestamp_mask_ = 0;
    double timestamp_period_ = 0;
    std::optional<GpuTimings> gpu_timings_;
    std::uint64_t timing_sample_id_ = 0;
    std::atomic_uint64_t validation_warnings_{0};
    std::atomic_uint64_t validation_errors_{0};
};
}
