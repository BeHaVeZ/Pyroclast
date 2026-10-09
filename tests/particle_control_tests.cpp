#include "app/explosion_renderer.hpp"
#include "app/sandbox.hpp"
#include "graphics/draw_validation.hpp"
#include "simulation/explosion_scenarios.hpp"

#include <array>
#include <bit>
#include <cmath>
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
            environment_uploaded |= initial.size() == pyroclast::terrain_layout::environment_bytes;
            prop_vertices_uploaded |= initial.size() == (18 * 36 + 7 * 84 + 4 * 132) * pyroclast::terrain_layout::prop_vertex_stride;
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
        if (description.vertex_shader == Shader::particle_lod_vertex && description.pixel_shader == Shader::particle_pixel &&
            description.blend == BlendMode::premultiplied_alpha)
            (description.depth == DepthMode::test ? lod_particles : lod_offscreen) = pipeline.get();
        if (description.vertex_shader == Shader::composite_vertex) composite = pipeline.get();
        if (description.vertex_shader == Shader::terrain_vertex) terrain = pipeline.get();
        if (description.vertex_shader == Shader::debris_vertex && description.depth == DepthMode::test_write) debris = pipeline.get();
        if (description.vertex_shader == Shader::sky_vertex) sky = pipeline.get();
        if (description.vertex_shader == Shader::debug_lines_vertex && description.topology == pyroclast::Topology::lines) lines = pipeline.get();
        if (description.vertex_shader == Shader::props_vertex && description.pixel_shader == Shader::props_pixel &&
            description.blend == BlendMode::opaque && description.depth == DepthMode::test_write) props = pipeline.get();
        if (description.vertex_shader == Shader::collider_lines_vertex && description.pixel_shader == Shader::debug_lines_pixel &&
            description.depth == DepthMode::test && description.topology == pyroclast::Topology::lines) colliders = pipeline.get();
        if (description.vertex_shader == Shader::showcase_lines_vertex && description.pixel_shader == Shader::debug_lines_pixel &&
            description.depth == DepthMode::test && description.topology == pyroclast::Topology::lines) showcase_lines = pipeline.get();
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
    pyroclast::SubmitTimings last_submit_timings() const override { return {}; }
    pyroclast::PresentInfo present_info() const override { return {"test", 0}; }
    std::optional<pyroclast::GpuMemory> memory_usage() const override { return std::nullopt; }
    std::uint64_t swapchain_rebuilds() const override { return 0; }
    pyroclast::RenderResult render(const pyroclast::Frame& frame) override
    {
        const char* error = pyroclast::validate_frame(frame);
        if (error) std::fprintf(stderr, "frame rejected: %s\n", error);
        pyroclast::FrameConstants received{};
        std::memcpy(&received, frame.constants.data(), std::min(frame.constants.size(), sizeof(received)));
        const bool realism_frame = pyroclast::realism_enabled(received.light_state[1]);
        valid_frame = error == nullptr &&
            frame.constants.size() == (realism_frame ? sizeof(pyroclast::FrameConstants) : pyroclast::legacy_frame_constant_bytes);
        if (valid_frame) constants = received;
        dispatch_groups = frame.dispatches.empty() ? 0 : frame.dispatches[0].groups[0];
        dispatches = frame.dispatches.size();
        dispatch_shaders.clear();
        for (const auto& dispatch : frame.dispatches) dispatch_shaders.push_back(shader_of(dispatch.pipeline));
        simulation_writes_arguments = !frame.dispatches.empty() && frame.dispatches[0].bindings.write[2] != nullptr;
        constant_bytes = frame.constants.size();
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
        const pyroclast::FrameConstants& sent = received;
        const bool props_shown = ((sent.light_state[1] >> pyroclast::particle_layout::props_mask_bit) & 1) != 0;
        const auto range_pass = [&](const pyroclast::Pass& pass) {
            std::vector<const pyroclast::GraphicsPipeline*> expected;
            if (props_shown) expected.push_back(props);
            expected.push_back(terrain);
            if (!frame.dispatches.empty()) expected.push_back(debris);
            expected.push_back(sky);
            if (props_shown && expect_boxes) expected.push_back(colliders);
            for (std::size_t mark = 0; mark < expect_marks.size(); ++mark) expected.push_back(showcase_lines);
            bool same = pass.draws.size() == expected.size();
            for (std::size_t index = 0; same && index < expected.size(); ++index) same = &pass.draws[index].pipeline == expected[index];
            if (same && props_shown)
            {
                const auto& drawn = pass.draws.front();
                same = drawn.vertex_count == prop_vertices && drawn.instance_count == 1 && drawn.bindings.read[0] &&
                    drawn.bindings.read[0] != environment && drawn.bindings.read[1] == environment;
            }
            if (same && props_shown && expect_boxes)
            {
                const auto& drawn = pass.draws[pass.draws.size() - 1 - expect_marks.size()];
                same = drawn.vertex_count == 24 && drawn.instance_count == boxes && drawn.constants[0] == boxes &&
                    drawn.bindings.read[0] == environment;
            }
            for (std::size_t mark = 0; same && mark < expect_marks.size(); ++mark)
            {
                const auto& drawn = pass.draws[pass.draws.size() - expect_marks.size() + mark];
                same = drawn.vertex_count == pyroclast::mark_vertices(expect_marks[mark]) && drawn.instance_count == 1 &&
                    drawn.constants == pyroclast::mark_constants(expect_marks[mark]) && drawn.bindings.read[0] == environment &&
                    !drawn.bindings.read[1];
            }
            return same && pass.target == scene && !pass.load && pass.depth == depth && pass.depth_write && inputs(pass, nullptr);
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
        half_light_passes = passes.size() == 4 && range_pass(passes[0]) && passes[1].target == half_target && !passes[1].depth &&
            with_depth(passes[1], nullptr) && draws(passes[1], {offscreen}) &&
            records(passes[1].draws[0], 0, ParticleSystem::capacity, layout::particle_arguments_offset) && passes[1].draws[0].constants[3] == 2 &&
            over_scene(passes[2]) && with_depth(passes[2], half_target) && draws(passes[2], {composite}) && present_pass(passes[3]);
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
    bool environment_uploaded = false;
    bool prop_vertices_uploaded = false;
    bool expect_boxes = false;
    std::vector<pyroclast::ShowcaseMark> expect_marks;
    std::uint32_t prop_vertices = 0;
    std::uint32_t boxes = 0;
    const pyroclast::Buffer* environment = nullptr;
    bool depth_cleared_far = false;
    bool valid_frame = false;
    bool range_only = false;
    bool full_resolution_passes = false;
    bool lined_passes = false;
    bool half_resolution_passes = false;
    bool half_light_passes = false;
    std::size_t dispatches = 0;
    std::uint32_t dispatch_groups = 0;
    std::vector<std::optional<pyroclast::Shader>> dispatch_shaders;
    bool simulation_writes_arguments = false;
    std::size_t constant_bytes = 0;
    pyroclast::Frame::Readback readback;
    pyroclast::RenderTarget* scene = nullptr;
    pyroclast::RenderTarget* depth = nullptr;
    pyroclast::RenderTarget* half_target = nullptr;
    pyroclast::FrameConstants constants{};
    const pyroclast::GraphicsPipeline* particles = nullptr;
    const pyroclast::GraphicsPipeline* offscreen = nullptr;
    const pyroclast::GraphicsPipeline* lod_particles = nullptr;
    const pyroclast::GraphicsPipeline* lod_offscreen = nullptr;
    const pyroclast::GraphicsPipeline* present = nullptr;
    const pyroclast::GraphicsPipeline* composite = nullptr;
    const pyroclast::GraphicsPipeline* terrain = nullptr;
    const pyroclast::GraphicsPipeline* debris = nullptr;
    const pyroclast::GraphicsPipeline* sky = nullptr;
    const pyroclast::GraphicsPipeline* lines = nullptr;
    const pyroclast::GraphicsPipeline* props = nullptr;
    const pyroclast::GraphicsPipeline* colliders = nullptr;
    const pyroclast::GraphicsPipeline* showcase_lines = nullptr;
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
    check(range.initialize(device, hdr) && device.environment_uploaded && device.prop_vertices_uploaded && device.terrain && device.sky &&
        device.props && device.colliders, "the range uploads its environment and the props' vertices, and creates their pipelines");
    check(range.prop_vertex_count() == 18 * 36 + 7 * 84 + 4 * 132 && range.environment().box_count() == 36, "the props' mesh");
    device.prop_vertices = range.prop_vertex_count();
    device.boxes = range.environment().box_count();
    device.environment = &range.environment_buffer();
    const auto submit = [](ParticleSystem& particles, double elapsed, std::uint32_t effects = 1) {
        const auto work = particles.plan(elapsed, effects);
        particles.commit(work);
        return work;
    };
    constexpr auto groups = ParticleSystem::groups_per_slab;

    ParticleSystem system;
    namespace layout = particle_layout;
    constexpr std::uint32_t all_groups = realism_layout::maximum_unit_groups;
    const auto classes = effect_classes();
    check(system.initialize(device, range.environment_buffer(), targets, classes) && device.zeroed_sizes == std::vector<std::uint32_t>{
        ParticleSystem::pool_records * layout::particle_stride, all_groups * 8, all_groups * 4, 3 * ParticleSystem::capacity * 4, layout::arguments_bytes},
        "a zero-filled pool of one slab per effect slot and the residual pool, alive masks and offsets per group, alive lists and arguments");
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
        simulation.bindings.read[0] == &range.environment_buffer() && simulation.bindings.read[1],
        "the simulation covers the active slabs, 40 groups each, by their classes, and writes their alive masks");
    const auto* class_buffer = simulation.bindings.read[1];
    check(scan.groups[0] == 1 && scan.constants[0] == 3 * groups && scan.constants[1] == system.tick() && scan.constants[2] == 0 &&
        scan.bindings.read[0] == masks && scan.bindings.read[1] == class_buffer &&
        offsets && arguments && arguments->size() == layout::arguments_bytes,
        "one scan group turns the masks into offsets and arguments stamped with the tick after the steps");
    check(scatter.groups[0] == 3 * groups && scatter.constants[0] == 3 && scatter.constants[1] == 0 && scatter.bindings.read[0] == masks &&
        scatter.bindings.read[1] == offsets && scatter.bindings.read[2] == class_buffer && alive && alive->size() == 3 * ParticleSystem::capacity * 4,
        "the scatter writes one list of every alive particle without offscreen smoke");
    check(three.counters.source == arguments && three.counters.offset == layout::counters_offset &&
        three.counters.size == layout::counters_basic_bytes, "the counter record's first 32 bytes are read back from the arguments");
    check(!scatter.bindings.read[3] && !scatter.bindings.write[1] && scatter.constants[2] == 0 && scan.constants[3] == 0,
        "by default the scatter only lists");
    const auto measured = system.plan(0, 3, 0, {true});
    const auto& measuring_scan = measured.dispatches[1];
    const auto& measuring = measured.dispatches[2];
    check(device.shader_of(measuring.pipeline) == Shader::particle_scatter_measure && measuring.bindings.read[3] == pool &&
        measuring.bindings.write[1] == arguments && measuring.bindings.read[0] == masks && measuring.bindings.write[0] == alive &&
        measuring.constants[2] == ParticleSystem::capacity && measuring_scan.constants[3] == layout::listing_measure &&
        measured.counters.size == layout::counters_bytes && measured.particles.bindings.read[1] == alive,
        "a measuring scatter reads the pool, adds to the counter record and the whole record is read back");
    const auto draws = [&](const Draw& draw, const GraphicsPipeline* pipeline, std::uint32_t first, std::uint32_t count,
        std::uint32_t depth_scale, std::uint32_t offset) {
        return &draw.pipeline == pipeline && draw.constants[0] == ParticleSystem::capacity && draw.constants[1] == first &&
            draw.constants[2] == count && draw.constants[3] == depth_scale && draw.indirect == arguments && draw.indirect_offset == offset &&
            draw.bindings.read[0] == pool && draw.bindings.read[1] == alive && draw.bindings.read[2] == class_buffer;
    };
    check(draws(three.particles, device.particles, 0, ParticleSystem::capacity, 1, layout::particle_arguments_offset),
        "every layer is drawn from the whole list");
    const auto layered = system.plan(0, 3, 2);
    ParticleListing thinning;
    thinning.lod_pixels = 24;
    thinning.faint_floor = 0.002f;
    const auto thinned = system.plan(0, 3, 0, thinning);
    const auto thinned_half = system.plan(0, 3, 2, thinning);
    check(device.lod_particles && device.lod_offscreen && &thinned.particles.pipeline == device.lod_particles &&
        thinned.particles.constants[4] == std::bit_cast<std::uint32_t>(24.0f) && thinned.particles.constants[5] == std::bit_cast<std::uint32_t>(0.002f) &&
        &thinned_half.offscreen_smoke.pipeline == device.lod_offscreen && thinned_half.offscreen_smoke.constants[4] == thinned.particles.constants[4] &&
        &thinned_half.light.pipeline == device.particles && &three.particles.pipeline == device.particles && three.particles.constants[4] == 0,
        "thinning draws smoke through the LOD shader; light and the default frame keep M18's draws");
    ParticleListing counting = thinning;
    counting.measure = true;
    const auto counted_lod = system.plan(0, 3, 0, counting);
    check(counted_lod.dispatches[2].constants[3] == thinned.particles.constants[4] && counted_lod.dispatches[2].constants[4] ==
        thinned.particles.constants[5], "the measuring scatter counts thinned and faint particles by the same radius and floor");
    const auto one_list = system.plan(0, 3, 2, {false, true});
    check(one_list.dispatches[1].constants[2] == 0 && one_list.dispatches[2].constants[1] == 0 &&
        draws(one_list.offscreen_particles, device.offscreen, 0, ParticleSystem::capacity, 2, layout::particle_arguments_offset),
        "half-resolution light keeps one list and draws all of it offscreen");
    const auto thinned_one_list = system.plan(0, 3, 2, {false, true, 24, 0.002f});
    check(&thinned_one_list.offscreen_particles.pipeline == device.lod_offscreen &&
        thinned_one_list.offscreen_particles.constants[4] == std::bit_cast<std::uint32_t>(24.0f) &&
        thinned_one_list.offscreen_particles.constants[5] == std::bit_cast<std::uint32_t>(0.002f) &&
        &one_list.offscreen_particles.pipeline == device.offscreen && one_list.offscreen_particles.constants[4] == 0,
        "half-resolution light thins through the LOD shader too");
    check(layered.dispatches[1].constants[2] == 1 && layered.dispatches[2].constants[1] == 1 &&
        draws(layered.offscreen_smoke, device.offscreen, 0, layout::light_list_first, 2, layout::smoke_arguments_offset) &&
        draws(layered.light, device.particles, layout::light_list_first, ParticleSystem::capacity, 1,
            layout::light_arguments_offset), "offscreen smoke and light are drawn from a list per layer");
    const auto& debris = three.debris;
    check(&debris.pipeline == device.debris && debris.constants[0] == ParticleSystem::capacity && debris.constants[1] == layout::debris_list_first &&
        debris.constants[2] == ParticleSystem::capacity && debris.indirect == arguments && debris.indirect_offset == layout::debris_arguments_offset &&
        debris.bindings.read[0] == pool && debris.bindings.read[1] == alive && debris.bindings.read[2] == class_buffer,
        "debris is drawn from its own list, opaque with depth");
    ReadbackData record;
    const std::uint32_t words[] = {7, 36, 10, 20, 6, 0, 0, 0};
    std::memcpy(record.bytes.data(), words, sizeof(words));
    record.size = sizeof(words);
    const auto counted = ParticleSystem::counters(record);
    check(counted && counted->tick == 7 && counted->alive == 36 && counted->smoke == 10 && counted->light == 20 && counted->debris == 6,
        "a counter record reads back");
    record.size = 16;
    check(!ParticleSystem::counters(record), "a readback of another size is no counter record");
    const std::uint32_t measured_words[] = {7, 36, 10, 20, 6, 4, 0, 0, 11, 12, 13, 14, 15, 0, 0, 0};
    std::memcpy(record.bytes.data(), measured_words, sizeof(measured_words));
    record.size = sizeof(measured_words);
    const auto whole = ParticleSystem::counters(record);
    check(whole && whole->offscreen == 4 && whole->smoke_coverage[0] == 11 && whole->smoke_coverage[3] == 14 && whole->light_coverage == 15,
        "a measured counter record reads back whole");
    record.size = sizeof(words);
    check(ParticleSystem::counters(record)->smoke_coverage[0] == 0 && ParticleSystem::counters(record)->light_coverage == 0,
        "a short record has no coverage words");

    ParticleSystem timed;
    timed.initialize(device, range.environment_buffer(), targets, classes);
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
    timed.set_time_scale(0.25);
    const auto slow_start = timed.tick();
    ticks = 0;
    for (int frame = 0; frame < 240; ++frame) ticks += submit(timed, 1.0 / 60).steps;
    check(ticks == 60 && timed.tick() == slow_start + 60, "0.25x advances 60 ticks in 4 wall seconds");
    const auto capped = timed.capped_frames();
    check(submit(timed, 1.0).steps == particle_layout::maximum_simulation_steps && submit(timed, 0).steps == 0 &&
        timed.capped_frames() == capped + 1, "at 0.25x a stall is still capped at 4 steps, and the capped frame is counted");
    timed.toggle_pause();
    timed.step();
    check(submit(timed, 1.0).steps == 1 && submit(timed, 1.0).steps == 0, "paused single steps ignore the time scale");
    timed.toggle_pause();
    timed.set_time_scale(1);
    timed.set_fixed_ticks(true);
    check(submit(timed, 0).steps == 1 && submit(timed, 5.0).steps == 1, "scripted runs advance one tick per frame whatever the elapsed time");
    timed.set_time_scale(0.25);
    check(submit(timed, 1.0 / 60).steps == 1, "scripted runs ignore the time scale");
    timed.set_time_scale(1);

    ParticleSystem profile;
    profile.initialize(device, range.environment_buffer(), targets, classes);
    profile.prepare_profile(180);
    profile.set_time_scale(0.25);
    profile.plan(1, 1);
    check(profile.has_pending_steps(), "skipped profile preparation retains ticks");
    for (int tick = 0; tick < 180; ++tick) check(submit(profile, 1).steps == 1, "profile preparation advances one fixed tick");
    check(!profile.has_pending_steps() && profile.tick() == 180, "profile reaches its target tick exactly");
    check(submit(profile, 1).steps == 0, "profile state stays frozen regardless of elapsed time");

    ParticleSystem rendered;
    rendered.initialize(device, range.environment_buffer(), targets, classes);
    ExplosionSystem effects(ParticleSystem::effect_slots);
    const auto camera = range.view(RangeView::overview);
    ExplosionRenderer full_resolution;
    check(full_resolution.initialize(device, hdr) && device.scene && device.scene->description().format == hdr && device.depth &&
        device.depth_cleared_far && device.half_target && device.half_target->description().divisor == 2 && device.composite &&
        device.present && device.lines,
        "the renderer creates a scene, a depth target cleared to the far end, and the half-size smoke target and its composite");
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
    const auto& drawn = device.constants;
    const auto expected_wind = wind_velocity(WindSettings{}, rendered.tick(), true);
    const float3 right = camera.right();
    const float3 up = camera.up();
    const float3 towards = cross(right, up);
    const auto rebuild = [&](const float4& billboard) { return right * billboard.x + up * billboard.y + towards * billboard.z; };
    const float3 sun{drawn.sun_direction.x, drawn.sun_direction.y, drawn.sun_direction.z};
    const auto near = [](float3 a, float3 b) { return std::abs(a.x - b.x) < 1e-5f && std::abs(a.y - b.y) < 1e-5f && std::abs(a.z - b.z) < 1e-5f; };
    check(drawn.clock[0] == rendered.tick() && drawn.wind.x == expected_wind.x && drawn.wind.z == expected_wind.z &&
        near(rebuild(drawn.sun_billboard), sun) && near(rebuild(drawn.up_billboard), {0, 1, 0}) &&
        dot(towards, camera.position - (camera.position + camera.forward())) > 0,
        "the frame carries its tick, the gusting wind, and the sun and up directions in billboard space");
    ExplosionRenderer::FrameOptions lined{};
    lined.views = {true, true, false, false};
    device.expect_boxes = true;
    check(device.showcase_lines && full_resolution.render(device, rendered, effects, range, camera, 1280, 720, 1.0 / 60, lined) ==
        RenderResult::submitted && device.valid_frame && device.lined_passes,
        "effect bounds are drawn before the particles, the collision boxes after the sky");
    auto hidden = lined;
    hidden.layer_mask &= ~(1u << layout::props_mask_bit);
    check(full_resolution.render(device, rendered, effects, range, camera, 1280, 720, 1.0 / 60, hidden) == RenderResult::submitted &&
        device.valid_frame && device.lined_passes, "hidden props: the range without them");
    ExplosionRenderer::FrameOptions boxes_only{};
    boxes_only.views.collision_boxes = true;
    check(full_resolution.render(device, rendered, effects, range, camera, 1280, 720, 1.0 / 60, boxes_only) == RenderResult::submitted &&
        device.valid_frame && device.full_resolution_passes, "the collision boxes alone add only their draw");
    device.expect_boxes = false;
    ExplosionRenderer::FrameOptions bounds_only{};
    bounds_only.views.effect_bounds = true;
    check(full_resolution.render(device, rendered, effects, range, camera, 1280, 720, 1.0 / 60, bounds_only) == RenderResult::submitted &&
        device.valid_frame && device.lined_passes, "the effect bounds alone add only their draw");
    std::vector<ShowcaseMark> marks(2);
    marks[0].segments = 32;
    marks[0].radius = 1;
    marks[1].kind = ShowcaseMark::wind_arrow;
    marks[1].lift = 15;
    ExplosionRenderer::FrameOptions marked{};
    marked.views.spawn_regions = true;
    marked.marks = marks;
    device.expect_marks = marks;
    check(full_resolution.render(device, rendered, effects, range, camera, 1280, 720, 1.0 / 60, marked) == RenderResult::submitted &&
        device.valid_frame && device.full_resolution_passes, "each showcase mark is one line draw after the sky, over the environment");
    marked.layer_mask &= ~(1u << layout::props_mask_bit);
    check(full_resolution.render(device, rendered, effects, range, camera, 1280, 720, 1.0 / 60, marked) == RenderResult::submitted &&
        device.valid_frame && device.full_resolution_passes, "hidden props keep the marks");
    device.expect_marks.clear();
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
    ordered_particles.initialize(ordered_device, ordered_range.environment_buffer(), targets, classes);
    ExplosionSystem ordered_effects(ParticleSystem::effect_slots);
    for (const auto& event : group_events(ordered_range.terrain(), 100)) ordered_effects.submit(event);
    ExplosionRenderer ordered;
    ordered.initialize(ordered_device, hdr);
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
    for (const auto key : {EffectOrder::none, EffectOrder::cloud})
    {
        ExplosionRenderer::FrameOptions keyed{};
        keyed.scalability.effect_order = key;
        ordered.render(ordered_device, ordered_particles, ordered_effects, ordered_range, oblique, 1280, 720, 0, keyed);
        std::vector<bool> listed(ParticleSystem::effect_slots);
        bool permutation = true;
        for (std::uint32_t rank = 0; rank < 100; ++rank)
        {
            const auto slot = ordered_device.constants.effect_order[rank / 4][rank % 4];
            permutation &= slot < 100 && !listed[slot] && (key != EffectOrder::none || slot == rank);
            if (slot < 100) listed[slot] = true;
        }
        check(permutation, key == EffectOrder::none ? "effect order none keeps slot order" : "effect order cloud is a permutation of the active slots");
    }

    ExplosionRenderer half_resolution;
    ExplosionSystem half_effects(ParticleSystem::effect_slots);
    half_effects.submit(group[0]);
    check(half_resolution.initialize(device, hdr) && device.half_target && device.half_target->description().divisor == 2 &&
        device.half_target->description().format == ExplosionRenderer::smoke_format && device.composite,
        "the half-size target holds smoke in its format");
    ExplosionRenderer::FrameOptions half_options{};
    half_options.scalability.smoke_resolution = SmokeResolution::half;
    check(half_resolution.render(device, rendered, half_effects, range, camera, 1280, 720, 1.0 / 60, half_options) == RenderResult::submitted &&
        device.valid_frame && device.half_resolution_passes && half_resolution.half_smoke(),
        "half resolution draws smoke into the target, then composites it under full-resolution light");
    check(half_resolution.render(device, rendered, half_effects, range, camera, 1280, 720, 1.0 / 60, {}) == RenderResult::submitted &&
        device.valid_frame && device.full_resolution_passes && !half_resolution.half_smoke(), "the next frame may draw at full resolution");
    half_options.scalability.half_light = true;
    check(half_resolution.render(device, rendered, half_effects, range, camera, 1280, 720, 1.0 / 60, half_options) == RenderResult::submitted &&
        device.valid_frame && device.half_light_passes, "with half-resolution light the target holds every translucent particle");
    half_options.scalability.half_light = false;
    half_resolution.render(device, rendered, half_effects, range, camera, 1280, 720, 1.0 / 60, {});
    ExplosionRenderer::FrameOptions automatic{};
    automatic.scalability.smoke_resolution = SmokeResolution::automatic;
    automatic.scalability.smoke_budget = {false, 100};
    check(half_resolution.render(device, rendered, half_effects, range, camera, 1280, 720, 1.0 / 60, automatic) == RenderResult::submitted &&
        device.valid_frame && device.full_resolution_passes && device.readback.size == layout::counters_bytes,
        "automatic without a record draws at full resolution and measures");
    ParticleCounters heavy_record;
    heavy_record.smoke_coverage[3] = 100 * 1280 * 720 / layout::coverage_unit_pixels;
    heavy_record.measured = true;
    automatic.counters = heavy_record;
    check(half_resolution.render(device, rendered, half_effects, range, camera, 1280, 720, 1.0 / 60, automatic) == RenderResult::submitted &&
        device.valid_frame && device.half_resolution_passes && half_resolution.half_smoke() && half_resolution.smoke_policy().switches() == 1,
        "automatic switches to half resolution at the budget");
    ExplosionRenderer::FrameOptions fixed_full{};
    fixed_full.counters = heavy_record;
    half_resolution.render(device, rendered, half_effects, range, camera, 1280, 720, 1.0 / 60, fixed_full);
    ParticleCounters unread_record;
    automatic.counters = unread_record;
    check(half_resolution.render(device, rendered, half_effects, range, camera, 1280, 720, 1.0 / 60, automatic) == RenderResult::submitted &&
        device.full_resolution_passes && !half_resolution.half_smoke() && half_resolution.smoke_policy().switches() == 1,
        "back in automatic, an unmeasured record keeps the full resolution just drawn");

    {
        Sandbox sandbox;
        std::array<Camera, CameraRig::bookmark_count> views;
        const std::array<std::string_view, CameraRig::bookmark_count> names{"overview", "ground", "hill", "aerial", "range", "yard", "far"};
        for (std::size_t index = 0; index < views.size(); ++index) views[index] = sandbox.range.view(static_cast<RangeView>(index));
        sandbox.camera = CameraRig(views[0], views, names);
        using Kind = UiCommand::Kind;
        const auto command = [](Kind kind, std::uint32_t value = 0, float scalar = 0) {
            UiCommand made{kind, value};
            made.scalar = scalar;
            return made;
        };
        const auto same_pose = [](const Camera& a, const Camera& b) {
            return a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z && a.yaw == b.yaw &&
                a.pitch == b.pitch;
        };

        apply(command(Kind::set_count, 2), sandbox);
        bool counted_ok = sandbox.showcase.count == 1;
        for (const std::uint32_t count : {10u, 50u, 100u, 1u})
        {
            apply(command(Kind::set_count, count), sandbox);
            counted_ok &= sandbox.showcase.count == count;
        }
        apply(command(Kind::set_count), sandbox);
        counted_ok &= sandbox.showcase.count == 10;
        check(counted_ok, "set_count takes 1, 10, 50 or 100, steps to the next on 0 and ignores other counts");
        apply(command(Kind::select_preset, 3), sandbox);
        apply(command(Kind::select_preset, preset_count), sandbox);
        check(sandbox.showcase.preset == static_cast<ExplosionPreset>(3), "select_preset ignores an index past the presets");

        apply(command(Kind::fire), sandbox);
        const auto salvo = sandbox.effects.pending();
        apply(command(Kind::fire, 1), sandbox);
        check(salvo == 10 && sandbox.effects.pending() == 11, "fire queues the selected count at the crosshair, or the number it carries");
        apply(command(Kind::barrage), sandbox);
        apply(command(Kind::toggle_bombardment), sandbox);
        check(sandbox.showcase.barrage && sandbox.showcase.bombardment && sandbox.showcase.barrage->preset == sandbox.showcase.preset,
            "barrage and toggle_bombardment start both at the crosshair with the selected preset");
        apply(command(Kind::set_bombardment, 4, 12), sandbox);
        const bool resized = sandbox.showcase.bombardment && sandbox.showcase.bombardment->interval == 4 &&
            sandbox.showcase.bombardment->radius == 12;
        apply(command(Kind::set_bombardment, 5, 100), sandbox);
        check(resized && sandbox.showcase.bombardment_interval == 4 && sandbox.showcase.bombardment_radius == Showcase::bombardment_radius_max,
            "set_bombardment restarts the running bombardment, ignores an unlisted interval and clamps the radius");
        apply(command(Kind::fire, 100), sandbox);
        apply(command(Kind::fire, 100), sandbox);
        sandbox.effects.start(0);
        sandbox.pressure.record(0, 0.0, static_cast<std::uint32_t>(sandbox.effects.active().size()), sandbox.effects.dropped());
        const bool pressed = sandbox.pressure.dropped_since_clear() > 0 && sandbox.pressure.peak_since_clear() == ParticleSystem::effect_slots;
        apply(command(Kind::clear), sandbox);
        check(pressed && !sandbox.showcase.barrage && !sandbox.showcase.bombardment && sandbox.effects.active().empty() &&
            sandbox.effects.pending() == 0 && sandbox.pressure.dropped_since_clear() == 0 && sandbox.pressure.peak_since_clear() == 0,
            "clear stops the barrage and bombardment, empties the slots and rebases the slot pressure");
        apply(command(Kind::barrage), sandbox);
        apply(command(Kind::toggle_bombardment), sandbox);
        sandbox.group = group_events(sandbox.range.terrain(), 10);
        apply(command(Kind::trigger), sandbox);
        check(!sandbox.showcase.barrage && !sandbox.showcase.bombardment && sandbox.effects.pending() == 10,
            "trigger stops both and restarts the startup group");
        apply(command(Kind::toggle_bombardment), sandbox);
        apply(command(Kind::toggle_bombardment), sandbox);
        check(!sandbox.showcase.bombardment, "toggle_bombardment stops a running bombardment");

        apply(command(Kind::toggle_slow_motion), sandbox);
        const bool slow = sandbox.particles.time_scale() == 0.25;
        apply(command(Kind::toggle_slow_motion), sandbox);
        check(slow && sandbox.particles.time_scale() == 1, "toggle_slow_motion switches between 0.25x and 1x");
        apply(command(Kind::toggle_pause), sandbox);
        const bool paused = sandbox.particles.paused();
        apply(command(Kind::toggle_pause), sandbox);
        check(paused && !sandbox.particles.paused(), "toggle_pause pauses and resumes");

        apply(command(Kind::toggle_walk), sandbox);
        const bool walking = sandbox.camera.mode() == CameraMode::walk;
        apply(command(Kind::toggle_walk), sandbox);
        check(walking && sandbox.camera.mode() == CameraMode::fly, "toggle_walk walks from the overview and flies again");
        apply(command(Kind::set_camera_speed, 0, 40), sandbox);
        check(sandbox.camera.speed() == 40, "set_camera_speed sets the active mode's speed");
        apply(command(Kind::recall_bookmark, 0), sandbox);
        const bool recalled = same_pose(sandbox.camera.camera(), views[0]);
        CameraInput forward{};
        forward.move = {0, 0, 1};
        sandbox.camera.update(forward, 1, sandbox.range.environment(), true);
        const auto moved = sandbox.camera.camera();
        apply(command(Kind::store_bookmark, 4), sandbox);
        apply(command(Kind::recall_bookmark, 1), sandbox);
        const bool other = same_pose(sandbox.camera.camera(), views[1]);
        apply(command(Kind::recall_bookmark, 4), sandbox);
        const bool stored = same_pose(sandbox.camera.camera(), moved) && sandbox.camera.bookmarks()[4].stored;
        apply(command(Kind::recall_bookmark, 7), sandbox);
        check(recalled && !same_pose(moved, views[0]) && other && stored && same_pose(sandbox.camera.camera(), moved),
            "store_bookmark keeps the pose, recall_bookmark returns to it, and a bookmark past the seventh is ignored");

        sandbox.views = {};
        apply(command(Kind::toggle_debug_lines), sandbox);
        const bool both_on = sandbox.views.effect_bounds && sandbox.views.collision_boxes && !sandbox.views.spawn_regions &&
            !sandbox.views.wind_arrow;
        apply(command(Kind::toggle_debug_view, 0), sandbox);
        const bool boxes_left = !sandbox.views.effect_bounds && sandbox.views.collision_boxes;
        apply(command(Kind::toggle_debug_lines), sandbox);
        check(both_on && boxes_left && !sandbox.views.effect_bounds && !sandbox.views.collision_boxes,
            "toggle_debug_lines turns the bounds and boxes on together, and both off while either is on");
        bool one_view = true;
        for (std::uint32_t view = 0; view < 4; ++view)
        {
            apply(command(Kind::toggle_debug_view, view), sandbox);
            const bool flags[] = {sandbox.views.effect_bounds, sandbox.views.collision_boxes, sandbox.views.spawn_regions,
                sandbox.views.wind_arrow};
            for (std::uint32_t index = 0; index < 4; ++index) one_view &= flags[index] == (index == view);
            apply(command(Kind::toggle_debug_view, view), sandbox);
        }
        apply(command(Kind::toggle_debug_view, 4), sandbox);
        check(one_view && !sandbox.views.effect_bounds && !sandbox.views.collision_boxes && !sandbox.views.spawn_regions &&
            !sandbox.views.wind_arrow, "toggle_debug_view flips only its view and ignores an unknown one");
        apply(command(Kind::set_smoke_resolution, 2), sandbox);
        const bool automatic_set = sandbox.scalability.smoke_resolution == SmokeResolution::automatic;
        apply(command(Kind::set_smoke_resolution, 3), sandbox);
        apply(command(Kind::set_smoke_budget, 1, 50000), sandbox);
        const bool counted_budget = sandbox.scalability.smoke_budget.count && sandbox.scalability.smoke_budget.limit == 50000;
        apply(command(Kind::set_smoke_budget, 0, -1), sandbox);
        check(automatic_set && sandbox.scalability.smoke_resolution == SmokeResolution::automatic && counted_budget &&
            sandbox.scalability.smoke_budget.limit == 50000, "set_smoke_resolution and set_smoke_budget take valid values only");
        apply(command(Kind::set_smoke_resolution, 0), sandbox);
        apply(command(Kind::set_smoke_lod, 0, 32), sandbox);
        apply(command(Kind::set_faint_floor, 0, 0.004f), sandbox);
        const bool lod_set = sandbox.scalability.smoke_lod == 32 && sandbox.scalability.faint_floor == 0.004f;
        apply(command(Kind::set_smoke_lod, 0, -1), sandbox);
        apply(command(Kind::set_faint_floor, 0, 1.5f), sandbox);
        check(lod_set && sandbox.scalability.smoke_lod == 32 && sandbox.scalability.faint_floor == 0.004f,
            "set_smoke_lod and set_faint_floor take valid values only");
        apply(command(Kind::set_smoke_lod, 0, 0), sandbox);
        apply(command(Kind::set_faint_floor, 0, 0), sandbox);
        apply(command(Kind::toggle_coverage), sandbox);
        const bool covering = sandbox.scalability.coverage;
        apply(command(Kind::toggle_coverage), sandbox);
        check(covering && !sandbox.scalability.coverage, "toggle_coverage switches the coverage counters");
        apply(command(Kind::set_effect_order, 2), sandbox);
        const bool cloud_order = sandbox.scalability.effect_order == EffectOrder::cloud;
        apply(command(Kind::set_effect_order, 3), sandbox);
        apply(command(Kind::set_slot_policy, 1), sandbox);
        const bool evicting = sandbox.scalability.slot_policy == SlotPolicy::evict_oldest;
        apply(command(Kind::set_slot_policy, 2), sandbox);
        check(cloud_order && sandbox.scalability.effect_order == EffectOrder::cloud && evicting &&
            sandbox.scalability.slot_policy == SlotPolicy::evict_oldest, "set_effect_order and set_slot_policy take valid values only");
        apply(command(Kind::set_effect_order, 0), sandbox);
        apply(command(Kind::set_slot_policy, 0), sandbox);
        check(sandbox.scalability.effect_order == EffectOrder::origin && sandbox.scalability.slot_policy == SlotPolicy::drop_new,
            "set_effect_order and set_slot_policy return to the defaults");
        apply(command(Kind::toggle_layer, layout::props_mask_bit), sandbox);
        const bool hidden_props = !sandbox.props_shown();
        apply(command(Kind::toggle_layer, layout::props_mask_bit), sandbox);
        check(hidden_props && sandbox.props_shown() && sandbox.layer_mask == layout::full_mask, "toggle_layer flips one mask bit");
    }

    {
        TestDevice realism_device;
        TestRange realism_range;
        realism_range.initialize(realism_device, hdr);
        realism_device.prop_vertices = realism_range.prop_vertex_count();
        realism_device.boxes = realism_range.environment().box_count();
        realism_device.environment = &realism_range.environment_buffer();
        ParticleSystem lingering;
        lingering.initialize(realism_device, realism_range.environment_buffer(), targets, classes);
        lingering.set_fixed_ticks(true);
        ExplosionSystem lingering_effects(ParticleSystem::effect_slots);
        ExplosionRenderer realism_renderer;
        realism_renderer.initialize(realism_device, hdr);
        ExplosionRenderer::FrameOptions options{};
        options.layer_mask = realism_layout::realism_full_mask;
        for (const auto& event : group_events(realism_range.terrain(), 1, ExplosionPreset::artillery_he)) lingering_effects.submit(event);
        const auto render = [&]() {
            return realism_renderer.render(realism_device, lingering, lingering_effects, realism_range, camera, 1280, 720, 1.0 / 60, options);
        };
        const std::vector<std::optional<Shader>> realism_dispatches{Shader::particle_realism_simulation, Shader::particle_scan_units,
            Shader::particle_scatter_units};
        check(render() == RenderResult::submitted && realism_device.valid_frame && realism_device.dispatch_shaders == realism_dispatches &&
            realism_device.dispatch_groups == groups && realism_device.simulation_writes_arguments &&
            realism_device.constant_bytes == sizeof(FrameConstants) && realism_device.readback.size == layout::counters_bytes &&
            realism_device.constants.unit_state == std::array<std::uint32_t, 4>{1, groups, 0, 0} &&
            realism_device.constants.unit_order[0][0] == 0u && realism_renderer.particles_drawn(),
            "a realism frame: the realism entries over one slab's groups, every constant, the whole counter record");
        const auto& slot = lingering_effects.slots()[0];
        check(slot.residual && (realism_device.constants.effect_residual[0][2] & realism_layout::latched_linger) != 0 &&
            realism_device.constants.effect_residual[0][1] == slot.residual->graduation_tick, "the slot's link to its cluster is in the constants");
        const auto graduation = slot.residual->graduation_tick;
        const auto natural_end = lingering_effects.residual().find(*slot.residual)->natural_end;
        while (lingering.tick() < graduation) render();
        check(render() == RenderResult::submitted && lingering_effects.active().empty() && realism_device.valid_frame &&
            realism_device.dispatches == 3 && realism_device.dispatch_groups == 3 &&
            realism_device.constants.unit_state == std::array<std::uint32_t, 4>{1, 3, 1, 0} &&
            realism_device.constants.unit_order[0][0] == realism_layout::unit_cluster_first + lingering_effects.residual().clusters()[0].slot &&
            realism_renderer.particles_drawn() && realism_renderer.listed_clusters() == 1,
            "after its slab retires, the effect's cluster alone is simulated and drawn");
        while (lingering.tick() < natural_end) render();
        check(render() == RenderResult::submitted && realism_device.dispatches == 0 && realism_device.range_only &&
            !realism_renderer.particles_drawn() && lingering_effects.residual().clusters().empty(),
            "once its last record has died, nothing is simulated");
        check(full_resolution.render(device, rendered, effects, range, camera, 1280, 720, 1.0 / 60, {}) == RenderResult::submitted &&
            device.constant_bytes == legacy_frame_constant_bytes, "a legacy frame uploads exactly M21's frame constants");
    }

    return failures == 0 ? 0 : 1;
}
