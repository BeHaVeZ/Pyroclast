#include "app/explosion_renderer.hpp"
#include "graphics/draw_validation.hpp"

#include <cstdio>
#include <cstring>

namespace
{
class TestDevice final : public pyroclast::GraphicsDevice
{
public:
    std::unique_ptr<pyroclast::Buffer> create_buffer(std::uint32_t size, std::span<const std::byte> initial) override
    {
        if (!initial.empty())
        {
            heights_uploaded = initial.size() == pyroclast::Terrain::resolution * pyroclast::Terrain::resolution * sizeof(float);
            return std::make_unique<pyroclast::Buffer>(size);
        }
        constexpr std::size_t stride = pyroclast::particle_layout::particle_stride;
        constexpr std::size_t largest_pool = pyroclast::maximum_explosions * pyroclast::ParticleSystem::explosion_particles;
        valid_upload = size % stride == 0 && size >= pyroclast::ParticleSystem::minimum_capacity * stride && size <= largest_pool * stride;
        return std::make_unique<pyroclast::Buffer>(size);
    }
    std::unique_ptr<pyroclast::GraphicsPipeline> create_graphics_pipeline(
        const pyroclast::GraphicsPipelineDescription& description) override
    {
        using pyroclast::BlendMode;
        using pyroclast::DepthMode;
        using pyroclast::Shader;
        auto pipeline = std::make_unique<pyroclast::GraphicsPipeline>(description.blend, description.depth);
        const bool particle = description.vertex_shader == Shader::particle_vertex;
        const bool composite_shader = description.vertex_shader == Shader::composite_vertex;
        if (particle && description.blend == BlendMode::premultiplied_alpha)
            (description.depth == DepthMode::test ? particles : offscreen) = pipeline.get();
        if (composite_shader) (description.blend == BlendMode::opaque ? present : composite) = pipeline.get();
        if (description.vertex_shader == Shader::terrain_vertex) terrain = pipeline.get();
        if (description.vertex_shader == Shader::sky_vertex) sky = pipeline.get();
        return pipeline;
    }
    std::unique_ptr<pyroclast::ComputePipeline> create_compute_pipeline(pyroclast::Shader) override
    {
        return std::make_unique<pyroclast::ComputePipeline>();
    }
    std::unique_ptr<pyroclast::RenderTarget> create_render_target(const pyroclast::RenderTargetDescription& description) override
    {
        auto created = std::make_unique<pyroclast::RenderTarget>(description);
        (description.format == pyroclast::TargetFormat::depth ? depth : description.divisor == 2 ? half_target : scene) = created.get();
        if (description.format == pyroclast::TargetFormat::depth) depth_cleared_far = description.clear_depth == 0;
        return created;
    }
    bool resize(int, int) override { return true; }
    bool wait_idle() override { return true; }
    std::optional<pyroclast::GpuTimings> completed_gpu_timings() const override { return std::nullopt; }
    std::optional<pyroclast::CapturedImage> read_capture() override { return std::nullopt; }
    std::string_view adapter_name() const override { return "test device"; }
    pyroclast::RenderResult render(const pyroclast::Frame& frame) override
    {
        valid_frame = pyroclast::validate_frame(frame) == nullptr && frame.dispatches.size() == 1 &&
            frame.constants.size() == sizeof(pyroclast::FrameConstants);
        if (valid_frame) std::memcpy(&constants, frame.constants.data(), sizeof(constants));
        const auto draws = [](const pyroclast::Pass& pass, std::initializer_list<const pyroclast::GraphicsPipeline*> pipelines) {
            if (pass.draws.size() != pipelines.size()) return false;
            std::size_t index = 0;
            for (const auto* pipeline : pipelines) if (&pass.draws[index++].pipeline != pipeline) return false;
            return true;
        };
        const auto inputs = [](const pyroclast::Pass& pass, const pyroclast::RenderTarget* first) {
            return pass.inputs[0] == first && !pass.inputs[1];
        };
        const auto& passes = frame.passes;
        const auto range_pass = [&](const pyroclast::Pass& pass) {
            return pass.target == scene && !pass.load && pass.depth == depth && pass.depth_write && inputs(pass, nullptr) &&
                draws(pass, {terrain, sky});
        };
        const auto present_pass = [&](const pyroclast::Pass& pass) {
            return !pass.target && !pass.depth && inputs(pass, scene) && draws(pass, {present}) && pass.draws[0].vertex_count == 3;
        };
        const auto over_scene = [&](const pyroclast::Pass& pass) {
            return pass.target == scene && pass.load && pass.depth == depth && !pass.depth_write;
        };
        const auto with_depth = [&](const pyroclast::Pass& pass, const pyroclast::RenderTarget* first) {
            return pass.inputs[0] == first && pass.inputs[1] == depth;
        };
        const auto records = [](const pyroclast::Draw& draw, std::uint32_t first, std::uint32_t count) {
            return draw.constants[2] == first && draw.constants[3] == count;
        };
        using pyroclast::ParticleSystem;
        full_resolution_passes = passes.size() == 3 && range_pass(passes[0]) && over_scene(passes[1]) && with_depth(passes[1], nullptr) &&
            draws(passes[1], {particles}) && records(passes[1].draws[0], 0, ParticleSystem::explosion_particles) && present_pass(passes[2]);
        half_resolution_passes = passes.size() == 4 && range_pass(passes[0]) &&
            passes[1].target == half_target && !passes[1].depth && with_depth(passes[1], nullptr) && draws(passes[1], {offscreen}) &&
            records(passes[1].draws[0], 0, ParticleSystem::smoke_particles) && passes[1].draws[0].constants[4] == 2 &&
            over_scene(passes[2]) && with_depth(passes[2], half_target) && draws(passes[2], {composite, particles}) &&
            records(passes[2].draws[1], ParticleSystem::smoke_particles, ParticleSystem::emissive_particles) &&
            passes[2].draws[0].vertex_count == 3 && present_pass(passes[3]);
        return result;
    }

    bool valid_upload = false;
    bool heights_uploaded = false;
    bool depth_cleared_far = false;
    bool valid_frame = false;
    bool full_resolution_passes = false;
    bool half_resolution_passes = false;
    pyroclast::RenderTarget* scene = nullptr;
    pyroclast::RenderTarget* depth = nullptr;
    pyroclast::RenderTarget* half_target = nullptr;
    pyroclast::FrameConstants constants{};
    const pyroclast::GraphicsPipeline* particles = nullptr;
    const pyroclast::GraphicsPipeline* offscreen = nullptr;
    const pyroclast::GraphicsPipeline* present = nullptr;
    const pyroclast::GraphicsPipeline* composite = nullptr;
    const pyroclast::GraphicsPipeline* terrain = nullptr;
    const pyroclast::GraphicsPipeline* sky = nullptr;
    pyroclast::RenderResult result = pyroclast::RenderResult::submitted;
};
}

int main()
{
    using namespace pyroclast;
    TestDevice device;
    TestRange range;
    int failures = 0;
    const auto check = [&failures](bool value, const char* name) {
        if (!value) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
    };
    check(range.initialize(device) && device.heights_uploaded && device.terrain && device.sky, "the range uploads its heights");
    const auto initialize = [&range](ParticleSystem& particles, TestDevice& owner, std::uint32_t explosions = 1) {
        return particles.initialize(owner, range.terrain(), range.heights(), explosions);
    };
    ParticleSystem system;
    const auto submit = [](ParticleSystem& particles, double elapsed) {
        const auto work = particles.plan(elapsed);
        particles.commit(work);
        return work;
    };
    const auto layered = [&range](const TestDevice& owner, const ParticleSystem& particles, const ParticleWork& work, std::uint32_t explosions) {
        const auto* pool = &particles.particles();
        const auto draws = [&](const Draw& draw, const GraphicsPipeline* pipeline, std::uint32_t first, std::uint32_t count) {
            return &draw.pipeline == pipeline && draw.constants[0] == particles.capacity() && draw.constants[1] == explosions &&
                draw.constants[2] == first && draw.constants[3] == count && draw.constants[4] == 1 && draw.vertex_count == 6 &&
                draw.instance_count == explosions * count && draw.bindings.read[0] == pool;
        };
        return draws(work.particles, owner.particles, 0, ParticleSystem::explosion_particles) &&
            draws(work.offscreen_smoke, owner.offscreen, 0, ParticleSystem::smoke_particles) &&
            draws(work.light, owner.particles, ParticleSystem::smoke_particles, ParticleSystem::emissive_particles) &&
            work.dispatch.bindings.write[0] == pool && work.dispatch.bindings.read[0] == &range.heights();
    };
    constexpr auto group_size = particle_layout::simulation_group_size;

    check(initialize(system, device) && device.valid_upload, "bounded zero-initialized pool");
    const auto origins = system.effect_origins();
    check(origins.size() == 1 && origins[0].x == 0 && origins[0].z == 0 && origins[0].y == range.terrain().height(0, 0) + 0.5f,
        "a single effect stands in the middle of the range");
    const auto first = submit(system, 1.0 / 60);
    check(first.dispatch.constants[0] == ParticleSystem::minimum_capacity &&
        first.dispatch.groups[0] == ParticleSystem::minimum_capacity / group_size &&
        first.dispatch.constants[2] == ParticleSystem::explosion_particles, "default dispatch");
    check(first.steps == 1 && first.dispatch.constants[1] == 1, "fixed simulation step");
    check(layered(device, system, first, 1), "particles are drawn effect by effect, smoke before light");
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
    initialize(timed, device);
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
        check(initialize(stress, stress_device, count), "stress initialization");
        const auto started = submit(stress, 0);
        const auto burst = count * ParticleSystem::explosion_particles;
        const auto expected_capacity = count == 1 ? ParticleSystem::minimum_capacity : burst;
        check(stress_device.valid_upload && stress.capacity() == expected_capacity &&
            stress.particles().size() == expected_capacity * particle_layout::particle_stride &&
            started.dispatch.constants[0] == expected_capacity && started.dispatch.constants[2] == burst &&
            started.dispatch.groups[0] == (expected_capacity + group_size - 1) / group_size &&
            layered(stress_device, stress, started, count),
            "bounded stress dispatch and per-effect draws");
        const auto group = stress.effect_origins();
        float sum_x = 0;
        float sum_z = 0;
        bool grounded = group.size() == count;
        for (const auto& origin : group)
        {
            sum_x += origin.x;
            sum_z += origin.z;
            grounded = grounded && origin.y == range.terrain().height(origin.x, origin.z) + 0.5f;
        }
        check(grounded && std::abs(sum_x) < 1e-3f && std::abs(sum_z) < 1e-3f &&
            (count == 1 || group[1].x - group[0].x == ParticleSystem::effect_spacing), "effect origins form a centred grid on the ground");
        stress.reset();
        const auto emptied = submit(stress, 0);
        check(emptied.dispatch.constants[2] == 0 && layered(stress_device, stress, emptied, count), "clear retains scenario layout");
        stress.trigger_explosion();
        check(submit(stress, 0).dispatch.constants[2] == burst, "restart restores selected group");
    }

    ParticleSystem profile;
    initialize(profile, device);
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
    check(!initialize(invalid, device, 0) && !initialize(invalid, device, 101), "invalid scenarios fail before allocation");

    ParticleSystem rendered;
    initialize(rendered, device);
    const auto camera = range.view(RangeView::overview);
    ExplosionRenderer full_resolution;
    check(full_resolution.initialize(device, false) && device.scene && device.depth && device.depth_cleared_far && !device.half_target &&
        device.present, "full resolution creates a scene and a depth target cleared to the far end");
    check(full_resolution.render(device, rendered, range, camera, 1280, 720, 1.0 / 60) == RenderResult::submitted &&
        device.valid_frame && device.full_resolution_passes,
        "full resolution draws the range, then smoke and light over it, then presents the scene");
    rendered.toggle_pause();
    rendered.step();
    device.result = RenderResult::skipped;
    check(full_resolution.render(device, rendered, range, camera, 1, 1, 1.0 / 60) == RenderResult::skipped && rendered.has_pending_steps(),
        "skipped frame is reported and keeps its queued step");
    device.result = RenderResult::submitted;
    full_resolution.render(device, rendered, range, camera, 1280, 720, 1.0 / 60);
    check(!rendered.has_pending_steps(), "submitted frame consumes its queued step");

    TestDevice ordered_device;
    TestRange ordered_range;
    ParticleSystem group;
    ExplosionRenderer ordered;
    ordered_range.initialize(ordered_device);
    group.initialize(ordered_device, ordered_range.terrain(), ordered_range.heights(), 100);
    ordered.initialize(ordered_device, false);
    auto oblique = camera;
    oblique.position = {-70, 30, 20};
    check(ordered.render(ordered_device, group, ordered_range, oblique, 1280, 720, 0) == RenderResult::submitted && ordered_device.valid_frame,
        "a 100-effect frame is valid");
    const auto group_origins = group.effect_origins();
    const auto distance = [&](std::uint32_t effect) {
        const auto offset = float3{group_origins[effect].x, group_origins[effect].y, group_origins[effect].z} - oblique.position;
        return dot(offset, offset);
    };
    std::array<bool, maximum_explosions> seen{};
    bool back_to_front = true;
    for (std::uint32_t rank = 0; rank < maximum_explosions; ++rank)
    {
        const auto effect = ordered_device.constants.effect_order[rank / 4][rank % 4];
        back_to_front = back_to_front && effect < maximum_explosions && !seen[effect];
        if (!back_to_front) break;
        seen[effect] = true;
        if (rank > 0) back_to_front = distance(ordered_device.constants.effect_order[(rank - 1) / 4][(rank - 1) % 4]) >= distance(effect);
    }
    check(back_to_front, "the effect order is a permutation from the farthest to the nearest effect");
    check(ordered_device.constants.effect_origins[99].x == group_origins[99].x && ordered_device.constants.effect_origins[99].z == group_origins[99].z,
        "the frame constants carry the effect origins");

    ExplosionRenderer half_resolution;
    check(half_resolution.initialize(device, true) && device.half_target && device.half_target->description().divisor == 2 &&
        device.composite, "half resolution creates a half-size target and its composite pipeline");
    check(half_resolution.render(device, rendered, range, camera, 1280, 720, 1.0 / 60) == RenderResult::submitted &&
        device.valid_frame && device.half_resolution_passes,
        "half resolution draws smoke into the target, then composites it under full-resolution light");

    return failures == 0 ? 0 : 1;
}
