#include "app/explosion_renderer.hpp"
#include "graphics/draw_validation.hpp"

#include <cstdio>

namespace
{
class TestDevice final : public pyroclast::GraphicsDevice
{
public:
    std::unique_ptr<pyroclast::VertexBuffer> create_vertex_buffer(
        std::span<const std::byte> bytes, std::uint32_t record_stride) override
    {
        constexpr std::size_t stride = pyroclast::particle_layout::particle_stride;
        constexpr std::size_t largest_pool = pyroclast::maximum_explosions * pyroclast::ParticleSystem::explosion_particles;
        valid_upload = record_stride == stride &&
            bytes.size() >= pyroclast::ParticleSystem::minimum_capacity * stride && bytes.size() <= largest_pool * stride;
        for (const auto byte : bytes) valid_upload &= byte == std::byte{0};
        return std::make_unique<pyroclast::VertexBuffer>(static_cast<std::uint32_t>(bytes.size() / stride), record_stride);
    }
    std::unique_ptr<pyroclast::GraphicsPipeline> create_graphics_pipeline(
        const pyroclast::GraphicsPipelineDescription& description) override
    {
        auto pipeline = std::make_unique<pyroclast::GraphicsPipeline>(description.blend);
        if (description.blend == pyroclast::BlendMode::premultiplied_alpha) smoke = pipeline.get();
        if (description.blend == pyroclast::BlendMode::additive) emissive = pipeline.get();
        return pipeline;
    }
    std::unique_ptr<pyroclast::ComputePipeline> create_compute_pipeline(
        pyroclast::Shader, const pyroclast::VertexBuffer& buffer) override
    {
        return std::make_unique<pyroclast::ComputePipeline>(buffer);
    }
    std::unique_ptr<pyroclast::RenderTarget> create_render_target(const pyroclast::RenderTargetDescription& description) override
    {
        auto created = std::make_unique<pyroclast::RenderTarget>(description);
        target = created.get();
        return created;
    }
    bool resize(int, int) override { return true; }
    bool wait_idle() override { return true; }
    std::optional<pyroclast::GpuTimings> completed_gpu_timings() const override { return std::nullopt; }
    std::string_view adapter_name() const override { return "test device"; }
    pyroclast::RenderResult render(const pyroclast::Frame& frame) override
    {
        valid_frame = pyroclast::validate_frame(frame) == nullptr && frame.compute;
        const auto draws = [](const pyroclast::Pass& pass, std::initializer_list<const pyroclast::GraphicsPipeline*> pipelines) {
            if (pass.draws.size() != pipelines.size()) return false;
            std::size_t index = 0;
            for (const auto* pipeline : pipelines) if (&pass.draws[index++].pipeline != pipeline) return false;
            return true;
        };
        const auto& passes = frame.passes;
        one_pass = passes.size() == 1 && !passes[0].background && draws(passes[0], {smoke, emissive});
        smoke_then_light_passes = passes.size() == 2 && target &&
            passes[0].target == target && !passes[0].background && draws(passes[0], {smoke}) &&
            !passes[1].target && passes[1].background == target && draws(passes[1], {emissive});
        return result;
    }

    bool valid_upload = false;
    bool valid_frame = false;
    bool one_pass = false;
    bool smoke_then_light_passes = false;
    pyroclast::RenderTarget* target = nullptr;
    const pyroclast::GraphicsPipeline* smoke = nullptr;
    const pyroclast::GraphicsPipeline* emissive = nullptr;
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
    const auto submit = [](ParticleSystem& particles, double elapsed) {
        const auto work = particles.plan(elapsed);
        particles.commit(work);
        return work;
    };
    const auto layered = [](const TestDevice& owner, const ParticleSystem& particles, const ParticleWork& work, std::uint32_t explosions) {
        const auto emissive_count = explosions * ParticleSystem::emissive_particles;
        return &work.smoke.pipeline == owner.smoke && &work.emissive.pipeline == owner.emissive &&
            work.smoke.layer == particle_layout::particle_layer_smoke &&
            work.emissive.layer == particle_layout::particle_layer_emissive &&
            work.smoke.first_record == emissive_count && work.smoke.record_count == particles.capacity() - emissive_count &&
            work.emissive.first_record == 0 && work.emissive.record_count == emissive_count;
    };
    constexpr auto group_size = particle_layout::simulation_group_size;

    check(system.initialize(device) && device.valid_upload, "bounded zero-initialized pool");
    const auto first = submit(system, 1.0 / 60);
    check(first.dispatch.constants[0] == ParticleSystem::minimum_capacity &&
        first.dispatch.group_count == ParticleSystem::minimum_capacity / group_size &&
        first.dispatch.constants[2] == ParticleSystem::explosion_particles, "default dispatch");
    check(first.steps == 1 && first.dispatch.constants[1] == 1, "fixed simulation step");
    check(layered(device, system, first, 1), "smoke and light cover the bounded pool as two contiguous layers");
    system.toggle_pause();
    check(submit(system, 1.0 / 60).steps == 0, "pause does not advance simulation");
    system.step();
    system.plan(1.0 / 60);
    check(submit(system, 1.0 / 60).steps != 0, "skipped submission preserves queued step");
    check(submit(system, 1.0 / 60).steps == 0, "step advances exactly once");
    system.step();
    system.reset();
    const auto epoch = system.plan(1.0 / 60).dispatch.constants[3];
    const auto cleared = submit(system, 1.0 / 60);
    check(epoch == 2 && cleared.dispatch.constants[3] == epoch && cleared.steps == 0,
        "reset persists across skipped frames and clears queued steps");
    check(cleared.dispatch.constants[2] == 0, "clear requests no new particles");
    system.step();
    system.trigger_explosion();
    const auto trigger_epoch = system.plan(1.0 / 60).dispatch.constants[3];
    const auto triggered = submit(system, 1.0 / 60);
    check(triggered.dispatch.constants[2] == ParticleSystem::explosion_particles && triggered.dispatch.constants[3] == trigger_epoch &&
        triggered.steps == 0, "trigger survives skipped submission, retains pause, clears pending steps");
    check(submit(system, 1.0 / 60).dispatch.constants[3] == trigger_epoch, "trigger epoch does not advance each frame");
    for (int trigger = 0; trigger < 10; ++trigger) system.trigger_explosion();
    const auto retriggered = submit(system, 1.0 / 60);
    check(retriggered.dispatch.constants[2] == ParticleSystem::explosion_particles && retriggered.dispatch.constants[3] == trigger_epoch + 10,
        "repeated triggers replace the burst without growing particle count");
    system.toggle_pause();
    check(submit(system, 1.0 / 60).steps == 1, "triggered burst advances after resuming");

    ParticleSystem timed;
    timed.initialize(device);
    std::uint32_t ticks = 0;
    for (int frame = 0; frame < 240; ++frame) ticks += submit(timed, 1.0 / 240).steps;
    check(ticks == 60, "240 Hz presentation still advances 60 simulation ticks per second");
    check(submit(timed, 1.0).steps == particle_layout::maximum_simulation_steps, "catch-up work is bounded after a stall");
    check(submit(timed, 0).steps == 0, "excess catch-up time is discarded");
    timed.plan(1.0 / 60);
    check(submit(timed, 0).steps == 1, "skipped submission preserves accumulated simulation time");

    for (const auto count : {1u, 10u, 50u, 100u})
    {
        TestDevice stress_device;
        ParticleSystem stress;
        check(stress.initialize(stress_device, count), "stress initialization");
        const auto started = submit(stress, 0);
        const auto burst = count * ParticleSystem::explosion_particles;
        const auto expected_capacity = count == 1 ? ParticleSystem::minimum_capacity : burst;
        check(stress_device.valid_upload && stress.capacity() == expected_capacity &&
            stress.particles().record_count() == expected_capacity &&
            started.dispatch.constants[0] == expected_capacity && started.dispatch.constants[2] == burst &&
            started.dispatch.group_count == (expected_capacity + group_size - 1) / group_size &&
            layered(stress_device, stress, started, count),
            "bounded stress dispatch and layer-contiguous ranges");
        stress.reset();
        const auto emptied = submit(stress, 0);
        check(emptied.dispatch.constants[2] == 0 && layered(stress_device, stress, emptied, count), "clear retains scenario layout");
        stress.trigger_explosion();
        check(submit(stress, 0).dispatch.constants[2] == burst, "restart restores selected group");
    }

    ParticleSystem profile;
    profile.initialize(device);
    profile.prepare_profile(180);
    profile.plan(1);
    check(profile.has_pending_steps(), "skipped profile preparation retains ticks");
    for (int tick = 0; tick < 180; ++tick)
    {
        check(submit(profile, 1).steps == 1, "profile preparation advances one fixed tick");
    }
    check(!profile.has_pending_steps(), "profile reaches target tick exactly");
    check(submit(profile, 1).steps == 0, "profile state stays frozen regardless of elapsed time");

    ParticleSystem invalid;
    check(!invalid.initialize(device, 0) &&
        !invalid.initialize(device, 101), "invalid scenarios fail before allocation");

    ParticleSystem rendered;
    rendered.initialize(device);
    ExplosionRenderer full_resolution;
    check(full_resolution.initialize(device, false) && !device.target, "full resolution creates no render target");
    check(full_resolution.render(device, rendered, 16.0f / 9.0f, 1.0 / 60) == RenderResult::submitted &&
        device.valid_frame && device.one_pass, "full resolution draws smoke then light in one backbuffer pass");
    rendered.toggle_pause();
    rendered.step();
    device.result = RenderResult::skipped;
    check(full_resolution.render(device, rendered, 1, 1.0 / 60) == RenderResult::skipped && rendered.has_pending_steps(),
        "skipped frame is reported and keeps its queued step");
    device.result = RenderResult::submitted;
    full_resolution.render(device, rendered, 1, 1.0 / 60);
    check(!rendered.has_pending_steps(), "submitted frame consumes its queued step");

    ExplosionRenderer half_resolution;
    check(half_resolution.initialize(device, true) && device.target && device.target->description().divisor == 2,
        "half resolution creates a half-size target");
    check(half_resolution.render(device, rendered, 16.0f / 9.0f, 1.0 / 60) == RenderResult::submitted &&
        device.valid_frame && device.smoke_then_light_passes,
        "half resolution draws smoke into the target, then stretches it under full-resolution light");

    return failures == 0 ? 0 : 1;
}
