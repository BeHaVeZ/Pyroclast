#include "app/explosion_renderer.hpp"
#include "graphics/draw_validation.hpp"
#include "simulation/explosion_scenarios.hpp"

#include <cstdio>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace
{
class TestDevice final : public pyroclast::GraphicsDevice
{
public:
    std::unique_ptr<pyroclast::Buffer> create_buffer(std::uint32_t size, std::span<const std::byte> initial) override
    {
        if (!initial.empty())
        {
            heights_uploaded |= initial.size() == pyroclast::Terrain::resolution * pyroclast::Terrain::resolution * sizeof(float);
            if (initial.size() == pyroclast::particle_layout::maximum_effect_classes * sizeof(pyroclast::EffectClass))
            {
                uploaded_classes.resize(pyroclast::particle_layout::maximum_effect_classes);
                std::memcpy(uploaded_classes.data(), initial.data(), initial.size());
            }
            return std::make_unique<pyroclast::Buffer>(size);
        }
        zeroed_sizes.push_back(size);
        return std::make_unique<pyroclast::Buffer>(size);
    }
    std::unique_ptr<pyroclast::GraphicsPipeline> create_graphics_pipeline(
        const pyroclast::GraphicsPipelineDescription& description) override
    {
        using pyroclast::BlendMode;
        using pyroclast::DepthMode;
        using pyroclast::Shader;
        auto pipeline = std::make_unique<pyroclast::GraphicsPipeline>(description.blend, description.depth, description.target);
        const bool particle = description.vertex_shader == Shader::particle_vertex;
        if (description.vertex_shader == Shader::tonemap_vertex) present = pipeline.get();
        if (particle && description.blend == BlendMode::premultiplied_alpha)
            (description.depth == DepthMode::test ? particles : offscreen) = pipeline.get();
        if (description.vertex_shader == Shader::composite_vertex) composite = pipeline.get();
        if (description.vertex_shader == Shader::terrain_vertex) terrain = pipeline.get();
        if (description.vertex_shader == Shader::sky_vertex) sky = pipeline.get();
        if (description.vertex_shader == Shader::debug_lines_vertex && description.topology == pyroclast::Topology::lines) lines = pipeline.get();
        return pipeline;
    }
    std::unique_ptr<pyroclast::ComputePipeline> create_compute_pipeline(pyroclast::Shader shader) override
    {
        auto pipeline = std::make_unique<pyroclast::ComputePipeline>();
        compute_shaders.emplace_back(pipeline.get(), shader);
        return pipeline;
    }
    std::optional<pyroclast::Shader> shader_of(const pyroclast::ComputePipeline& pipeline) const
    {
        for (const auto& [created, shader] : compute_shaders) if (created == &pipeline) return shader;
        return std::nullopt;
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
    std::optional<pyroclast::ReadbackData> completed_readback() const override { return std::nullopt; }
    std::optional<pyroclast::CapturedImage> read_capture() override { return std::nullopt; }
    std::string_view adapter_name() const override { return "test device"; }
    pyroclast::RenderResult render(const pyroclast::Frame& frame) override
    {
        const char* error = pyroclast::validate_frame(frame);
        if (error) std::fprintf(stderr, "frame rejected: %s\n", error);
        valid_frame = error == nullptr && frame.constants.size() == sizeof(pyroclast::FrameConstants);
        if (valid_frame) std::memcpy(&constants, frame.constants.data(), sizeof(constants));
        dispatch_groups = frame.dispatches.empty() ? 0 : frame.dispatches[0].groups[0];
        dispatches = frame.dispatches.size();
        readback = frame.readback;
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
        const auto records = [](const pyroclast::Draw& draw, std::uint32_t first, std::uint32_t count, std::uint32_t arguments) {
            return draw.constants[1] == first && draw.constants[2] == count && draw.indirect && draw.indirect_offset == arguments;
        };
        namespace layout = pyroclast::particle_layout;
        using pyroclast::ParticleSystem;
        range_only = passes.size() == 2 && range_pass(passes[0]) && present_pass(passes[1]);
        full_resolution_passes = passes.size() == 3 && range_pass(passes[0]) && over_scene(passes[1]) && with_depth(passes[1], nullptr) &&
            draws(passes[1], {particles}) && records(passes[1].draws[0], 0, ParticleSystem::capacity, layout::particle_arguments_offset) &&
            present_pass(passes[2]);
        lined_passes = passes.size() == 3 && range_pass(passes[0]) && over_scene(passes[1]) && draws(passes[1], {lines, particles}) &&
            passes[1].draws[0].vertex_count == 32 && present_pass(passes[2]);
        half_resolution_passes = passes.size() == 4 && range_pass(passes[0]) &&
            passes[1].target == half_target && !passes[1].depth && with_depth(passes[1], nullptr) && draws(passes[1], {offscreen}) &&
            records(passes[1].draws[0], 0, layout::light_list_first, layout::smoke_arguments_offset) && passes[1].draws[0].constants[3] == 2 &&
            over_scene(passes[2]) && with_depth(passes[2], half_target) && draws(passes[2], {composite, particles}) &&
            records(passes[2].draws[1], layout::light_list_first, ParticleSystem::capacity, layout::light_arguments_offset) &&
            passes[2].draws[0].vertex_count == 3 && present_pass(passes[3]);
        return result;
    }

    std::vector<std::uint32_t> zeroed_sizes;
    std::vector<pyroclast::EffectClass> uploaded_classes;
    std::vector<std::pair<const pyroclast::ComputePipeline*, pyroclast::Shader>> compute_shaders;
    bool heights_uploaded = false;
    bool depth_cleared_far = false;
    bool valid_frame = false;
    bool range_only = false;
    bool full_resolution_passes = false;
    bool lined_passes = false;
    bool half_resolution_passes = false;
    std::size_t dispatches = 0;
    std::uint32_t dispatch_groups = 0;
    pyroclast::Frame::Readback readback;
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
    const pyroclast::GraphicsPipeline* lines = nullptr;
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
    constexpr auto hdr = TargetFormat::r11g11b10_float;
    static_assert(hdr != ExplosionRenderer::smoke_format);
    constexpr ParticleTargets targets{hdr, ExplosionRenderer::smoke_format};
    check(range.initialize(device, hdr) && device.heights_uploaded && device.terrain && device.sky, "the range uploads its heights");
    const auto submit = [](ParticleSystem& particles, double elapsed, std::uint32_t effects = 1) {
        const auto work = particles.plan(elapsed, effects);
        particles.commit(work);
        return work;
    };
    constexpr auto groups = ParticleSystem::groups_per_slab;

    ParticleSystem system;
    namespace layout = particle_layout;
    constexpr std::uint32_t all_groups = ParticleSystem::effect_slots * groups;
    const auto classes = effect_classes();
    check(system.initialize(device, range.heights(), targets, classes) && device.zeroed_sizes == std::vector<std::uint32_t>{
        ParticleSystem::capacity * layout::particle_stride, all_groups * 8, all_groups * 4, 2 * ParticleSystem::capacity * 4, layout::arguments_bytes},
        "a zero-filled pool with one slab per effect slot, alive masks and offsets per group, alive lists and arguments");
    bool uploaded = device.uploaded_classes.size() == layout::maximum_effect_classes;
    for (std::size_t index = 0; uploaded && index < device.uploaded_classes.size(); ++index)
    {
        const auto& expected = index < classes.size() ? classes[index] : EffectClass{};
        uploaded = std::memcmp(&device.uploaded_classes[index], &expected, sizeof(EffectClass)) == 0;
    }
    check(uploaded, "the effect classes are uploaded once, in index order, the rest zero");
    const auto idle = system.plan(1.0 / 60, 0);
    check(idle.effects == 0 && idle.steps == 1, "no active effect: nothing to simulate or draw");
    const auto three = submit(system, 1.0 / 60, 3);
    const auto* pool = &system.particles();
    const auto& [simulation, scan, scatter] = three.dispatches;
    const auto* masks = simulation.bindings.write[1];
    const auto* offsets = scan.bindings.write[0];
    const auto* arguments = scan.bindings.write[1];
    const auto* alive = scatter.bindings.write[0];
    check(device.shader_of(simulation.pipeline) == Shader::particle_simulation && device.shader_of(scan.pipeline) == Shader::particle_scan &&
        device.shader_of(scatter.pipeline) == Shader::particle_scatter, "simulation, scan and scatter, in that order");
    check(simulation.groups[0] == 3 * groups && three.steps == 2 && simulation.constants[0] == three.steps && simulation.constants[1] == 3 &&
        simulation.constants[2] == ParticleSystem::capacity && simulation.bindings.write[0] == pool && masks &&
        simulation.bindings.read[0] == &range.heights() && simulation.bindings.read[1],
        "the simulation covers the active slabs, 40 groups each, by their classes, and writes their alive masks");
    const auto* class_buffer = simulation.bindings.read[1];
    check(scan.groups[0] == 1 && scan.constants[0] == 3 * groups && scan.constants[1] == system.tick() && scan.constants[2] == 0 &&
        scan.bindings.read[0] == masks && scan.bindings.read[1] == class_buffer &&
        offsets && arguments && arguments->size() == layout::arguments_bytes,
        "one scan group turns the masks into offsets and arguments stamped with the tick after the steps");
    check(scatter.groups[0] == 3 * groups && scatter.constants[0] == 3 && scatter.constants[1] == 0 && scatter.bindings.read[0] == masks &&
        scatter.bindings.read[1] == offsets && scatter.bindings.read[2] == class_buffer && alive && alive->size() == 2 * ParticleSystem::capacity * 4,
        "the scatter writes one list of every alive particle without offscreen smoke");
    check(three.counters.source == arguments && three.counters.offset == layout::counters_offset &&
        three.counters.size == sizeof(ParticleCounters), "the counter record is read back from the arguments");
    const auto draws = [&](const Draw& draw, const GraphicsPipeline* pipeline, std::uint32_t first, std::uint32_t count,
        std::uint32_t depth_scale, std::uint32_t offset) {
        return &draw.pipeline == pipeline && draw.constants[0] == ParticleSystem::capacity && draw.constants[1] == first &&
            draw.constants[2] == count && draw.constants[3] == depth_scale && draw.indirect == arguments && draw.indirect_offset == offset &&
            draw.bindings.read[0] == pool && draw.bindings.read[1] == alive && draw.bindings.read[2] == class_buffer;
    };
    check(draws(three.particles, device.particles, 0, ParticleSystem::capacity, 1, layout::particle_arguments_offset),
        "every layer is drawn from the whole list");
    const auto layered = system.plan(0, 3, 2);
    check(layered.dispatches[1].constants[2] == 1 && layered.dispatches[2].constants[1] == 1 &&
        draws(layered.offscreen_smoke, device.offscreen, 0, layout::light_list_first, 2, layout::smoke_arguments_offset) &&
        draws(layered.light, device.particles, layout::light_list_first, ParticleSystem::capacity, 1,
            layout::light_arguments_offset), "offscreen smoke and light are drawn from a list per layer");
    ReadbackData record;
    const std::uint32_t words[] = {7, 30, 10, 20};
    std::memcpy(record.bytes.data(), words, sizeof(words));
    record.size = sizeof(words);
    const auto counted = ParticleSystem::counters(record);
    check(counted && counted->tick == 7 && counted->alive == 30 && counted->smoke == 10 && counted->light == 20, "a counter record reads back");
    record.size = 12;
    check(!ParticleSystem::counters(record), "a readback of another size is no counter record");

    ParticleSystem timed;
    timed.initialize(device, range.heights(), targets, classes);
    std::uint32_t ticks = 0;
    for (int frame = 0; frame < 240; ++frame) ticks += submit(timed, 1.0 / 240).steps;
    check(ticks == 60 && timed.tick() == 60, "240 Hz presentation still advances 60 simulation ticks per second");
    check(submit(timed, 1.0).steps == particle_layout::maximum_simulation_steps, "catch-up work is bounded after a stall");
    check(submit(timed, 0).steps == 0, "excess catch-up time is discarded");
    timed.plan(1.0 / 60, 1);
    check(submit(timed, 0).steps == 1, "a skipped submission keeps its simulation time");
    timed.toggle_pause();
    const auto before_pause = timed.tick();
    check(submit(timed, 1.0 / 60).steps == 0 && timed.tick() == before_pause, "pause does not advance simulation");
    timed.step();
    timed.plan(1.0 / 60, 1);
    check(timed.has_pending_steps() && submit(timed, 1.0 / 60).steps == 1 && submit(timed, 1.0 / 60).steps == 0,
        "a single step survives a skipped frame and advances exactly once");
    timed.step();
    timed.discard_pending();
    check(!timed.has_pending_steps(), "discarding forgets queued steps");
    timed.toggle_pause();
    timed.set_fixed_ticks(true);
    check(submit(timed, 0).steps == 1 && submit(timed, 5.0).steps == 1, "scripted runs advance one tick per frame whatever the elapsed time");

    ParticleSystem profile;
    profile.initialize(device, range.heights(), targets, classes);
    profile.prepare_profile(180);
    profile.plan(1, 1);
    check(profile.has_pending_steps(), "skipped profile preparation retains ticks");
    for (int tick = 0; tick < 180; ++tick) check(submit(profile, 1).steps == 1, "profile preparation advances one fixed tick");
    check(!profile.has_pending_steps() && profile.tick() == 180, "profile reaches its target tick exactly");
    check(submit(profile, 1).steps == 0, "profile state stays frozen regardless of elapsed time");

    ParticleSystem rendered;
    rendered.initialize(device, range.heights(), targets, classes);
    ExplosionSystem effects(ParticleSystem::effect_slots);
    const auto camera = range.view(RangeView::overview);
    ExplosionRenderer full_resolution;
    check(full_resolution.initialize(device, false, hdr) && device.scene && device.scene->description().format == hdr && device.depth &&
        device.depth_cleared_far && !device.half_target && device.present && device.lines,
        "full resolution creates a scene and a depth target cleared to the far end");
    check(full_resolution.render(device, rendered, effects, range, camera, 1280, 720, 1.0 / 60, {}) == RenderResult::submitted &&
        device.valid_frame && device.range_only && device.dispatches == 0 && !device.readback.source, "without effects only the range is drawn");
    const auto group = group_events(range.terrain(), 10);
    for (const auto& event : group) effects.submit(event);
    const auto start_tick = rendered.tick();
    check(full_resolution.render(device, rendered, effects, range, camera, 1280, 720, 1.0 / 60, {}) == RenderResult::submitted &&
        device.valid_frame && device.full_resolution_passes && device.dispatches == 3 && device.dispatch_groups == 10 * groups &&
        device.readback.source && device.readback.offset == layout::counters_offset,
        "ten effects: simulation, scan and scatter over ten slabs, one indirect particle draw and the counters read back");
    bool table = effects.active().size() == 10;
    for (std::uint32_t slot = 0; slot < 10 && table; ++slot)
    {
        table = device.constants.effect_origins[slot].x == group[slot].position.x && device.constants.effect_origins[slot].z == group[slot].position.z &&
            device.constants.effect_normals[slot].y == group[slot].normal.y && device.constants.effect_states[slot][0] == group[slot].seed &&
            device.constants.effect_states[slot][1] == effects.slots()[slot].generation && device.constants.effect_states[slot][2] == start_tick;
    }
    check(table, "the frame constants carry every started effect's slot");
    check(full_resolution.render(device, rendered, effects, range, camera, 1280, 720, 1.0 / 60, {false, nullptr, true}) == RenderResult::submitted &&
        device.valid_frame && device.lined_passes, "effect lines are drawn before the particles");
    device.result = RenderResult::skipped;
    const auto before_skip = rendered.tick();
    check(full_resolution.render(device, rendered, effects, range, camera, 1280, 720, 1.0 / 60, {}) == RenderResult::skipped &&
        rendered.tick() == before_skip, "a skipped frame keeps its simulation time");
    device.result = RenderResult::submitted;
    rendered.set_fixed_ticks(true);
    for (std::uint32_t tick = rendered.tick(); tick < start_tick + effect_ticks(ExplosionPreset::small_he, SurfaceType::dirt); ++tick)
        full_resolution.render(device, rendered, effects, range, camera, 1280, 720, 0, {});
    check(effects.active().size() == 10, "effects live until their last particle has died");
    full_resolution.render(device, rendered, effects, range, camera, 1280, 720, 0, {});
    check(effects.active().empty() && device.range_only && device.dispatches == 0, "then they retire and nothing is simulated");

    TestDevice ordered_device;
    TestRange ordered_range;
    ordered_range.initialize(ordered_device, hdr);
    ParticleSystem ordered_particles;
    ordered_particles.initialize(ordered_device, ordered_range.heights(), targets, classes);
    ExplosionSystem ordered_effects(ParticleSystem::effect_slots);
    for (const auto& event : group_events(ordered_range.terrain(), 100)) ordered_effects.submit(event);
    ExplosionRenderer ordered;
    ordered.initialize(ordered_device, false, hdr);
    auto oblique = camera;
    oblique.position = {-70, 30, 20};
    check(ordered.render(ordered_device, ordered_particles, ordered_effects, ordered_range, oblique, 1280, 720, 0, {}) == RenderResult::submitted &&
        ordered_device.valid_frame && ordered_device.dispatch_groups == 100 * groups, "a 100-effect frame is valid");
    const auto slots = ordered_effects.slots();
    const auto distance = [&](std::uint32_t slot) {
        const auto offset = slots[slot].origin - oblique.position;
        return dot(offset, offset);
    };
    std::vector<bool> seen(ParticleSystem::effect_slots);
    bool back_to_front = true;
    for (std::uint32_t rank = 0; rank < 100 && back_to_front; ++rank)
    {
        const auto slot = ordered_device.constants.effect_order[rank / 4][rank % 4];
        back_to_front = slot < 100 && !seen[slot] && slots[slot].active;
        if (!back_to_front) break;
        seen[slot] = true;
        if (rank > 0) back_to_front = distance(ordered_device.constants.effect_order[(rank - 1) / 4][(rank - 1) % 4]) >= distance(slot);
    }
    check(back_to_front, "the effect order is a permutation of the active slots from the farthest to the nearest");

    ExplosionRenderer half_resolution;
    ExplosionSystem half_effects(ParticleSystem::effect_slots);
    half_effects.submit(group[0]);
    check(half_resolution.initialize(device, true, hdr) && device.half_target && device.half_target->description().divisor == 2 &&
        device.half_target->description().format == ExplosionRenderer::smoke_format && device.composite,
        "half resolution creates a half-size target and its composite pipeline");
    check(half_resolution.render(device, rendered, half_effects, range, camera, 1280, 720, 1.0 / 60, {}) == RenderResult::submitted &&
        device.valid_frame && device.half_resolution_passes,
        "half resolution draws smoke into the target, then composites it under full-resolution light");

    return failures == 0 ? 0 : 1;
}
