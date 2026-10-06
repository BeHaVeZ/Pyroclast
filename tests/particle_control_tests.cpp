#include "simulation/particle_system.hpp"
#include "graphics/particle_shaders.hpp"

#include <cstdio>

namespace pyroclast
{
GraphicsPipelineDescription particle_shaders(Renderer) { return {{}, {}, VertexLayout::particle, BlendMode::additive}; }
std::span<const std::byte> particle_compute_shader(Renderer) { return {}; }
}

namespace
{
class TestBuffer final : public pyroclast::VertexBuffer
{
public:
    explicit TestBuffer(std::uint32_t count) : count_(count) {}
    std::uint32_t vertex_count() const override { return count_; }
private:
    std::uint32_t count_;
};

class TestDevice final : public pyroclast::GraphicsDevice
{
public:
    std::unique_ptr<pyroclast::VertexBuffer> create_vertex_buffer(
        std::span<const std::byte> bytes, pyroclast::VertexLayout layout) override
    {
        valid_upload = layout == pyroclast::VertexLayout::particle && bytes.size() >= 8192 * 32 && bytes.size() <= 256000 * 32;
        for (const auto byte : bytes) valid_upload &= byte == std::byte{0};
        return std::make_unique<TestBuffer>(static_cast<std::uint32_t>(bytes.size() / 32));
    }
    std::unique_ptr<pyroclast::GraphicsPipeline> create_graphics_pipeline(
        const pyroclast::GraphicsPipelineDescription& description) override
    {
        auto pipeline = std::make_unique<pyroclast::GraphicsPipeline>();
        if (description.blend == pyroclast::BlendMode::premultiplied_alpha) smoke = pipeline.get();
        if (description.blend == pyroclast::BlendMode::additive) emissive = pipeline.get();
        return pipeline;
    }
    std::unique_ptr<pyroclast::ComputePipeline> create_compute_pipeline(
        std::span<const std::byte>, const pyroclast::VertexBuffer&) override
    {
        return std::make_unique<pyroclast::ComputePipeline>();
    }
    bool resize(int, int) override { return true; }
    bool wait_idle() override { return true; }
    std::optional<pyroclast::GpuTimings> completed_gpu_timings() const override { return std::nullopt; }
    pyroclast::RenderResult render(const pyroclast::DrawCommand& draw) override
    {
        valid_ranges = draw.ranges.size() == 2 &&
            &draw.ranges[0].pipeline == smoke && &draw.ranges[1].pipeline == emissive &&
            draw.ranges[0].first_record == expected_explosions * pyroclast::ParticleSystem::emissive_particles &&
            draw.ranges[0].record_count == draw.vertices.vertex_count() - expected_explosions * pyroclast::ParticleSystem::emissive_particles &&
            draw.ranges[1].first_record == 0 && draw.ranges[1].record_count == expected_explosions * pyroclast::ParticleSystem::emissive_particles;
        constants = draw.compute->constants;
        groups = draw.compute->group_count;
        return result;
    }

    std::uint32_t expected_explosions = 1;
    bool valid_upload = false;
    bool valid_ranges = false;
    const pyroclast::GraphicsPipeline* smoke = nullptr;
    const pyroclast::GraphicsPipeline* emissive = nullptr;
    std::array<std::uint32_t, 4> constants{};
    std::uint32_t groups = 0;
    pyroclast::RenderResult result = pyroclast::RenderResult::submitted;
};
}

int main()
{
    using namespace pyroclast;
    TestDevice device;
    ParticleSystem system;
    int failures = 0;
    const auto check = [&failures](bool value, const char* name) {
        if (!value) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
    };
    check(system.initialize(device, Renderer::d3d12) && device.valid_upload, "bounded zero-initialized pool");
    system.render(device, 16.0f / 9.0f, 1.0 / 60);
    check(device.constants[0] == 8192 && device.groups == 128 && device.constants[2] == ParticleSystem::explosion_particles, "default dispatch");
    check(device.constants[1] == 1, "fixed simulation step");
    check(device.valid_ranges, "smoke alpha draw precedes additive light and covers the bounded pool");
    system.toggle_pause();
    system.render(device, 1, 1.0 / 60);
    check(device.constants[1] == 0, "pause does not advance simulation");
    system.step();
    device.result = RenderResult::skipped;
    check(system.render(device, 1, 1.0 / 60) == RenderResult::skipped, "skipped submission is reported");
    device.result = RenderResult::submitted;
    system.render(device, 1, 1.0 / 60);
    check(device.constants[1] != 0, "skipped submission preserves queued step");
    system.render(device, 1, 1.0 / 60);
    check(device.constants[1] == 0, "step advances exactly once");
    system.step();
    system.reset();
    device.result = RenderResult::skipped;
    system.render(device, 1, 1.0 / 60);
    const auto epoch = device.constants[3];
    device.result = RenderResult::submitted;
    system.render(device, 1, 1.0 / 60);
    check(epoch == 2 && device.constants[3] == epoch && device.constants[1] == 0,
        "reset persists across skipped frames and clears queued steps");
    check(device.constants[2] == 0, "clear requests no new particles");
    system.step();
    system.trigger_explosion();
    device.result = RenderResult::skipped;
    system.render(device, 1, 1.0 / 60);
    const auto trigger_epoch = device.constants[3];
    device.result = RenderResult::submitted;
    system.render(device, 1, 1.0 / 60);
    check(device.constants[2] == ParticleSystem::explosion_particles && device.constants[3] == trigger_epoch &&
        device.constants[1] == 0, "trigger survives skipped submission, retains pause, clears pending steps");
    system.render(device, 1, 1.0 / 60);
    check(device.constants[3] == trigger_epoch, "trigger epoch does not advance each frame");
    for (int trigger = 0; trigger < 10; ++trigger) system.trigger_explosion();
    system.render(device, 1, 1.0 / 60);
    check(device.constants[2] == ParticleSystem::explosion_particles && device.constants[3] == trigger_epoch + 10,
        "repeated triggers replace the burst without growing particle count");
    system.toggle_pause();
    system.render(device, 1, 1.0 / 60);
    check(device.constants[1] == 1, "triggered burst advances after resuming");
    ParticleSystem timed;
    timed.initialize(device, Renderer::d3d12);
    std::uint32_t ticks = 0;
    for (int frame = 0; frame < 240; ++frame)
    {
        timed.render(device, 1, 1.0 / 240);
        ticks += device.constants[1];
    }
    check(ticks == 60, "240 Hz presentation still advances 60 simulation ticks per second");
    timed.render(device, 1, 1.0);
    check(device.constants[1] == 4, "catch-up work is bounded after a stall");
    timed.render(device, 1, 0);
    check(device.constants[1] == 0, "excess catch-up time is discarded");
    device.result = RenderResult::skipped;
    timed.render(device, 1, 1.0 / 60);
    device.result = RenderResult::submitted;
    timed.render(device, 1, 0);
    check(device.constants[1] == 1, "skipped submission preserves accumulated simulation time");
    for (const auto count : {1u, 10u, 50u, 100u})
    {
        TestDevice stress_device;
        stress_device.expected_explosions = count;
        ParticleSystem stress;
        check(stress.initialize(stress_device, Renderer::d3d12, count), "stress initialization");
        stress.render(stress_device, 1, 0);
        const auto expected_capacity = count == 1 ? 8192u : count * 2560;
        check(stress_device.valid_upload && stress.capacity() == expected_capacity &&
            stress_device.constants[0] == expected_capacity && stress_device.constants[2] == count * 2560 &&
            stress_device.groups == (expected_capacity + 63) / 64 && stress_device.valid_ranges,
            "bounded stress dispatch and layer-contiguous ranges");
        stress.reset();
        stress.render(stress_device, 1, 0);
        check(stress_device.constants[2] == 0 && stress_device.valid_ranges, "clear retains scenario layout");
        stress.trigger_explosion();
        stress.render(stress_device, 1, 0);
        check(stress_device.constants[2] == count * 2560, "restart restores selected group");
    }
    ParticleSystem invalid;
    check(!invalid.initialize(device, Renderer::d3d12, 0) &&
        !invalid.initialize(device, Renderer::d3d12, 101), "invalid scenarios fail before allocation");
    return failures == 0 ? 0 : 1;
}
