#include "explosion_renderer.hpp"
#include "realism_constants.hpp"
#include "rotor_constants.hpp"
#include "../graphics/realism_layout.hpp"
#include "../simulation/effect_lights.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <vector>

namespace pyroclast
{
bool ExplosionRenderer::initialize(GraphicsDevice& device, TargetFormat scene_format)
{
    scene_ = device.create_render_target({1, {}, scene_format});
    depth_ = device.create_render_target({1, {}, TargetFormat::depth, 0.0f});
    present_ = device.create_graphics_pipeline({Shader::tonemap_vertex, Shader::tonemap_pixel, BlendMode::opaque});
    present_waves_ = device.create_graphics_pipeline({Shader::tonemap_vertex, Shader::present_waves_pixel, BlendMode::opaque});
    wilson_ = device.create_graphics_pipeline({Shader::wilson_vertex, Shader::wilson_pixel, BlendMode::premultiplied_alpha, DepthMode::ignore, scene_format});
    for (const auto& effect : effect_classes()) blast_classes_.push_back(effect.blast);
    lines_ = device.create_graphics_pipeline(
        {Shader::debug_lines_vertex, Shader::debug_lines_pixel, BlendMode::opaque, DepthMode::test, scene_format, Topology::lines});
    showcase_lines_ = device.create_graphics_pipeline(
        {Shader::showcase_lines_vertex, Shader::debug_lines_pixel, BlendMode::opaque, DepthMode::test, scene_format, Topology::lines});
    if (!scene_ || !depth_ || !present_ || !present_waves_ || !wilson_ || !lines_ || !showcase_lines_ || !helicopter_.initialize(device, scene_format))
    {
        return false;
    }
    smoke_target_ = device.create_render_target({2, {0, 0, 0, 0}, smoke_format});
    composite_ = device.create_graphics_pipeline(
        {Shader::composite_vertex, Shader::composite_pixel, BlendMode::premultiplied_alpha, DepthMode::ignore, scene_format});
    return smoke_target_ && composite_;
}

RenderResult ExplosionRenderer::render(GraphicsDevice& device, ParticleSystem& particles, ExplosionSystem& effects,
    const TestRange& range, const Camera& camera, int width, int height, double elapsed_seconds, const FrameOptions& options)
{
    assert(width > 0 && height > 0);
    const bool realism = realism_enabled(options.layer_mask);
    const bool gusts = ((options.layer_mask >> particle_layout::gusts_mask_bit) & 1) != 0;
    const auto tick = particles.tick();
    const RealismStart realism_start{options.layer_mask, options.linger, drift_.at(tick, options.wind, gusts)};
    auto* rotorcraft = realism ? options.rotorcraft : nullptr;
    const bool stepping = !particles.paused() || particles.has_pending_steps();
    if (rotorcraft && rotorcraft->helicopter() && rotorcraft->request_pending() && stepping && tick >= rotorcraft->helicopter()->plan.list_tick)
    {
        const auto& helicopter = *rotorcraft->helicopter();
        effects.request_rotor(helicopter.seed, to_float3(flight_state(helicopter.plan, tick).position), rotorcraft->slot_end());
        rotorcraft->requested();
    }
    effects.start(tick, options.scalability.slot_policy, realism ? &realism_start : nullptr);
    const auto active = effects.active();
    auto units = realism ? count_units(effects, tick) : FrameUnits{};
    units.rotor = rotorcraft && rotorcraft->variants();
    const auto& scalability = options.scalability;
    const bool automatic = scalability.smoke_resolution == SmokeResolution::automatic;
    const double screen_pixels = static_cast<double>(width) * static_cast<double>(height);
    const bool half = scalability.smoke_resolution == SmokeResolution::half ||
        (automatic && smoke_policy_.decide(options.counters, scalability.smoke_budget, screen_pixels));
    const bool half_light = half && scalability.half_light;
    const auto work = particles.plan(elapsed_seconds, static_cast<std::uint32_t>(active.size()),
        half ? smoke_target_->description().divisor : 0,
        {scalability.coverage || automatic, half_light, scalability.smoke_lod, scalability.faint_floor}, realism ? &units : nullptr);

    FrameConstants constants{};
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    constants.view_projection = camera.projection(w / h) * camera.view();
    constants.inverse_view_projection = inverse(constants.view_projection);
    constants.camera_position = {camera.position.x, camera.position.y, camera.position.z, camera.near_plane};
    const auto right = camera.right();
    const auto up = camera.up();
    constants.camera_right = {right.x, right.y, right.z, 0};
    constants.camera_up = {up.x, up.y, up.z, 0};
    constants.viewport = {w, h, 1 / w, 1 / h};
    constants.projection = {camera.projection(w / h).columns[1].y * h * 0.5f, 0, 0, 0};
    TestRange::light(constants);
    const auto slots = effects.slots();
    assert(slots.size() == constants.effect_origins.size());
    for (std::size_t index = 0; index < slots.size(); ++index)
    {
        const auto& slot = slots[index];
        constants.effect_origins[index] = {slot.origin.x, slot.origin.y, slot.origin.z, 0};
        constants.effect_normals[index] = {slot.normal.x, slot.normal.y, slot.normal.z, 0};
        constants.effect_states[index] = {slot.seed, slot.generation, slot.start_tick, slot.effect_class};
    }
    std::vector<std::uint32_t> order(active.begin(), active.end());
    const auto key_tick = particles.tick() + work.steps;
    const auto effect_order = options.scalability.effect_order;
    const auto distance = [&slots, &camera, key_tick, effect_order](std::uint32_t slot) {
        auto centre = slots[slot].origin;
        if (effect_order == EffectOrder::cloud)
        {
            const float seconds = static_cast<float>(key_tick - std::min(key_tick, slots[slot].start_tick)) / 60.0f;
            centre.y += cloud_rise(slots[slot].preset, seconds);
        }
        const auto offset = centre - camera.position;
        return dot(offset, offset);
    };
    if (effect_order != EffectOrder::none)
    {
        std::stable_sort(order.begin(), order.end(), [&distance](std::uint32_t a, std::uint32_t b) { return distance(a) > distance(b); });
    }
    for (std::size_t rank = 0; rank < order.size(); ++rank) constants.effect_order[rank / 4][rank % 4] = order[rank];
    std::array<PointLight, particle_layout::maximum_lights> lights;
    const bool lit = ((options.layer_mask >> particle_layout::lights_mask_bit) & 1) != 0;
    const auto light_count = lit ? select_lights(slots, active, particles.tick() + work.steps, lights, realism) : 0;
    for (std::uint32_t index = 0; index < light_count; ++index)
    {
        const auto& light = lights[index];
        constants.light_positions[index] = {light.position.x, light.position.y, light.position.z, light.radius};
        constants.light_colors[index] = {light.color.x, light.color.y, light.color.z, light.source_radius};
    }
    constants.light_state = {light_count, options.layer_mask, 0, 0};
    lights_ = light_count;
    const auto drawn_tick = particles.tick() + work.steps;
    constants.clock = {drawn_tick, 0, 0, 0};
    const auto wind = wind_velocity(options.wind, drawn_tick, gusts);
    constants.wind = {wind.x, wind.y, wind.z, 0};
    const auto towards_camera = cross(right, up);
    const float3 sun{constants.sun_direction.x, constants.sun_direction.y, constants.sun_direction.z};
    constants.sun_billboard = {dot(right, sun), dot(up, sun), dot(towards_camera, sun), 0};
    constants.up_billboard = {right.y, up.y, towards_camera.y, 0};

    const auto opaque = range.draws();
    const bool props = ((options.layer_mask >> particle_layout::props_mask_bit) & 1) != 0;
    BlastList blasts;
    if (realism)
    {
        blasts = select_blasts(effects, blast_classes_, tick, key_tick, options.humidity);
        constants.blast_state = {blasts.count, std::bit_cast<std::uint32_t>(options.humidity), 0, 0};
        for (std::uint32_t entry = 0; entry < blasts.count; ++entry)
        {
            constants.blast_slots[entry / 4] |= blasts.slots[entry] << ((entry % 4) * 8);
            constants.blast_rings[entry] = blasts.rings[entry];
            constants.blast_fronts[entry] = blasts.fronts[entry];
            constants.blast_returns[entry] = blasts.returns[entry];
        }
    }
    const bool waves = blasts.count > 0 && ((options.layer_mask >> realism_layout::waves_mask_bit) & 1) != 0;
    const bool condensation = ((options.layer_mask >> realism_layout::condensation_mask_bit) & 1) != 0 &&
        std::any_of(blasts.rings.begin(), blasts.rings.begin() + blasts.count, [](const float4& ring) { return ring.w > 0; });
    const Draw wilson{*wilson_, {}, {}, 3, realism_layout::maximum_blasts, "Wilson shells"};
    const Draw present{waves ? *present_waves_ : *present_, {}, {}, 3, 1, waves ? "Tonemap and blast waves" : "Tonemap"};
    const Draw lines{*lines_, {}, {work.effects}, 32, std::max(work.effects, 1u), "Effect lines"};
    listed_clusters_ = 0;
    rotor_frame_ = {};
    if (realism)
    {
        if (rotorcraft)
        {
            const auto drift_at = [this, &options, gusts](std::uint32_t at) { return drift_.at(at, options.wind, gusts); };
            rotor_frame_ = prepare_rotor(*rotorcraft, effects, range.terrain(), options.wind, gusts, options.layer_mask, tick, work.steps, key_tick, drift_at);
        }
        const auto filled = fill_realism_constants(constants, effects, tick, key_tick, camera, effect_order, drift_.at(key_tick, options.wind, gusts),
            options.linger, rotor_frame_.slot ? std::optional<float3>(rotor_frame_.key) : std::nullopt,
            rotor_frame_.exists ? std::optional<float3>(rotor_frame_.disc_key) : std::nullopt);
        assert(filled.units.units == units.units && filled.units.groups == units.groups);
        listed_clusters_ = filled.clusters;
        if (rotorcraft)
        {
            fill_rotor_constants(constants, *rotorcraft, rotor_frame_);
        }
    }
    const bool simulate = realism ? work.units != 0 : work.effects != 0;
    particles_drawn_ = simulate;
    const bool show_lines = options.views.effect_bounds && simulate;
    std::vector<Draw> scene_draws;
    if (props) scene_draws.push_back(opaque.props);
    scene_draws.push_back(opaque.terrain);
    if (simulate) scene_draws.push_back(work.debris);
    std::vector<Draw> helicopter_shadow;
    const float3 sun_towards{constants.sun_direction.x, constants.sun_direction.y, constants.sun_direction.z};
    if (realism) helicopter_.scene_draws(rotor_frame_, range.environment_buffer(), sun_towards, scene_draws, helicopter_shadow);
    scene_draws.push_back(opaque.sky);
    for (const auto& draw : helicopter_shadow) scene_draws.push_back(draw);
    if (props && options.views.collision_boxes) scene_draws.push_back(opaque.colliders);
    Bindings environment{};
    environment.read[0] = &range.environment_buffer();
    for (const auto& mark : options.marks)
    {
        scene_draws.push_back({*showcase_lines_, environment, mark_constants(mark), mark_vertices(mark), 1, "Showcase lines"});
    }
    std::vector<Pass> passes{{scene_.get(), {}, scene_draws, "Scene", false, depth_.get(), true}};
    std::vector<Draw> over_scene;
    if (show_lines) over_scene.push_back(lines);
    if (condensation) over_scene.push_back(wilson);
    if (simulate && !half)
    {
        over_scene.push_back(work.particles);
        passes.push_back({scene_.get(), {nullptr, depth_.get()}, over_scene, "Particles", true, depth_.get(), false});
    }
    else if (simulate)
    {
        over_scene.push_back({*composite_, {}, {}, 3, 1, "Smoke composite"});
        if (!half_light) over_scene.push_back(work.light);
        passes.push_back({smoke_target_.get(), {nullptr, depth_.get()}, {half_light ? &work.offscreen_particles : &work.offscreen_smoke, 1},
            "Half-resolution smoke"});
        passes.push_back({scene_.get(), {smoke_target_.get(), depth_.get()}, over_scene, "Composite and light", true, depth_.get(), false});
    }
    passes.push_back({nullptr, {scene_.get(), waves ? depth_.get() : nullptr}, {&present, 1}, "Present"});
    auto constant_bytes = std::as_bytes(std::span(&constants, 1));
    if (!realism_enabled(options.layer_mask)) constant_bytes = constant_bytes.first(legacy_frame_constant_bytes);
    Frame frame{simulate ? std::span<const Dispatch>(work.dispatches) : std::span<const Dispatch>(), passes, {}, constant_bytes,
        options.capture, options.overlay};
    if (simulate) frame.readback = work.counters;
    const auto result = device.render(frame);
    if (result == RenderResult::submitted)
    {
        particles.commit(work);
        drift_.advance(particles.tick(), options.wind, gusts);
        if (automatic) smoke_policy_.commit(half);
        else smoke_policy_.follow(half);
        half_smoke_ = half;
        submitted_work_ = count_work(frame);
    }
    return result;
}
}
