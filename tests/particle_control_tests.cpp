#include "simulation/particle_system.hpp"
#include "graphics/particle_shaders.hpp"

#include <cstdio>

namespace pyroclast
{
GraphicsPipelineDescription particle_shaders(Renderer) { return {}; }
std::span<const std::byte> particle_compute_shader(Renderer) { return {}; }
}

namespace
{
class TestBuffer final : public pyroclast::VertexBuffer
{
public:
    std::uint32_t vertex_count() const override { return pyroclast::ParticleSystem::capacity; }
};

class TestDevice final : public pyroclast::GraphicsDevice
{
public:
    std::unique_ptr<pyroclast::VertexBuffer> create_vertex_buffer(
        std::span<const std::byte> bytes, pyroclast::VertexLayout layout) override
    {
        valid_upload = layout == pyroclast::VertexLayout::particle && bytes.size() == 8192 * 32;
        for (const auto byte : bytes) valid_upload &= byte == std::byte{0};
        return std::make_unique<TestBuffer>();
    }
    std::unique_ptr<pyroclast::GraphicsPipeline> create_graphics_pipeline(
        const pyroclast::GraphicsPipelineDescription&) override
    {
        return std::make_unique<pyroclast::GraphicsPipeline>();
    }
    std::unique_ptr<pyroclast::ComputePipeline> create_compute_pipeline(
        std::span<const std::byte>, const pyroclast::VertexBuffer&) override
    {
        return std::make_unique<pyroclast::ComputePipeline>();
    }
    bool resize(int, int) override { return true; }
    bool wait_idle() override { return true; }
    pyroclast::RenderResult render(const pyroclast::DrawCommand& draw) override
    {
        constants = draw.compute->constants;
        groups = draw.compute->group_count;
        return result;
    }

    bool valid_upload = false;
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
    check(device.constants[0] == 8192 && device.groups == 128 && device.constants[2] == 1, "default dispatch");
    check(device.constants[1] == 1, "fixed simulation step");
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
    system.toggle_emission();
    system.render(device, 1, 1.0 / 60);
    check(device.constants[2] == 0, "emission can stop independently of simulation");
    system.toggle_pause();
    system.render(device, 1, 1.0 / 60);
    check(device.constants[1] != 0 && device.constants[2] == 0, "drain continues after resuming");
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
    return failures == 0 ? 0 : 1;
}
