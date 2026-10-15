#include "particle_system.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstring>

namespace pyroclast
{
bool ParticleSystem::initialize(GraphicsDevice& device, const Buffer& environment, ParticleTargets targets,
    std::span<const EffectClass> classes)
{
    assert(!classes.empty() && classes.size() <= particle_layout::maximum_effect_classes);
    environment_ = &environment;
    const auto bytes = pool_records * static_cast<std::uint32_t>(sizeof(Particle));
    particles_ = device.create_buffer(bytes, {});
    constexpr std::uint32_t groups = realism_layout::maximum_unit_groups;
    alive_masks_ = device.create_buffer(groups * 8, {});
    offsets_ = device.create_buffer(groups * 4, {});
    alive_ = device.create_buffer(3 * capacity * 4, {});
    std::vector<EffectClass> table(particle_layout::maximum_effect_classes);
    std::copy(classes.begin(), classes.end(), table.begin());
    classes_ = device.create_buffer(static_cast<std::uint32_t>(table.size() * sizeof(EffectClass)), std::as_bytes(std::span(table)));
    arguments_ = device.create_buffer(particle_layout::arguments_bytes, {});
    if (!particles_ || !alive_masks_ || !offsets_ || !alive_ || !arguments_ || !classes_)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Particle buffers could not be created.");
        return false;
    }
    struct ModelShaders
    {
        Shader vertex, lod_vertex, pixel, simulation, scan, scatter, scatter_measure;
    };
    constexpr std::array<ModelShaders, particle_model_count> shaders{{
        {Shader::particle_vertex, Shader::particle_lod_vertex, Shader::particle_pixel, Shader::particle_simulation, Shader::particle_scan,
            Shader::particle_scatter, Shader::particle_scatter_measure},
        {Shader::particle_realism_vertex, Shader::particle_realism_lod_vertex, Shader::particle_realism_pixel,
            Shader::particle_realism_simulation, Shader::particle_scan_units, Shader::particle_scatter_units, Shader::particle_scatter_units_measure},
        {Shader::particle_rotor_vertex, Shader::particle_rotor_lod_vertex, Shader::particle_rotor_pixel, Shader::particle_rotor_simulation,
            Shader::particle_scan_units, Shader::particle_scatter_units, Shader::particle_scatter_rotor_measure},
    }};
    constexpr const char* model_names[] = {"legacy", "realism", "aircraft"};
    const auto translucent = [&device](Shader vertex, Shader pixel, DepthMode depth, TargetFormat target) {
        return device.create_graphics_pipeline({vertex, pixel, BlendMode::premultiplied_alpha, depth, target});
    };
    for (std::size_t model = 0; model < particle_model_count; ++model)
    {
        const auto& shader = shaders[model];
        auto& draw = draws_[model];
        draw.scene = translucent(shader.vertex, shader.pixel, DepthMode::test, targets.scene);
        draw.offscreen = translucent(shader.vertex, shader.pixel, DepthMode::none, targets.offscreen);
        draw.lod_scene = translucent(shader.lod_vertex, shader.pixel, DepthMode::test, targets.scene);
        draw.lod_offscreen = translucent(shader.lod_vertex, shader.pixel, DepthMode::none, targets.offscreen);
        auto& compute = computes_[model];
        compute.simulation = device.create_compute_pipeline(shader.simulation);
        compute.scan = device.create_compute_pipeline(shader.scan);
        compute.scatter = device.create_compute_pipeline(shader.scatter);
        compute.scatter_measure = device.create_compute_pipeline(shader.scatter_measure);
        if (!draw.scene || !draw.offscreen || !draw.lod_scene || !draw.lod_offscreen || !compute.simulation || !compute.scan ||
            !compute.scatter || !compute.scatter_measure)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "The %s particle pipelines could not be created.", model_names[model]);
            return false;
        }
    }
    debris_ = device.create_graphics_pipeline({Shader::debris_vertex, Shader::debris_pixel, BlendMode::opaque, DepthMode::test_write, targets.scene});
    if (!debris_)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "The debris pipeline could not be created.");
        return false;
    }
    SDL_Log("Particle pool ready: %u effect slots of %u particles and a residual pool of %u, %u bytes, fixed step=1/60 s.", effect_slots,
        slab_records, realism_layout::residual_records, bytes);
    return true;
}

ParticleWork ParticleSystem::plan(std::uint32_t tick, std::uint32_t steps, std::uint32_t active_effects, std::uint32_t offscreen_divisor,
    ParticleListing listing, const FrameUnits* units) const
{
    assert(particles_ && environment_ && debris_);
    assert(steps <= particle_layout::maximum_simulation_steps);
    assert(active_effects <= effect_slots);
    Bindings simulation{};
    simulation.read[0] = environment_;
    simulation.read[1] = classes_.get();
    simulation.write[0] = particles_.get();
    simulation.write[1] = alive_masks_.get();
    Bindings scan{};
    scan.read[0] = alive_masks_.get();
    scan.read[1] = classes_.get();
    scan.write[0] = offsets_.get();
    scan.write[1] = arguments_.get();
    Bindings scatter{};
    scatter.read[0] = alive_masks_.get();
    scatter.read[1] = offsets_.get();
    scatter.read[2] = classes_.get();
    scatter.write[0] = alive_.get();
    if (listing.measure)
    {
        scatter.read[3] = particles_.get();
        scatter.write[1] = arguments_.get();
    }
    if (units) simulation.write[2] = arguments_.get();
    Bindings drawing{};
    drawing.read[0] = particles_.get();
    drawing.read[1] = alive_.get();
    drawing.read[2] = classes_.get();
    const std::uint32_t dispatched = units ? units->groups : active_effects * groups_per_slab;
    const auto groups = std::max(dispatched, 1u);
    const bool layered = offscreen_divisor != 0 && !listing.offscreen_light;
    constexpr auto light_first = particle_layout::light_list_first;
    const std::uint32_t records = units ? pool_records : capacity;
    const std::uint32_t counted = units ? units->units : active_effects;
    const DrawConstants parameters{steps, counted, records, tick};
    const std::uint32_t flags = listing.measure ? particle_layout::listing_measure : 0u;
    const DrawConstants scan_parameters{dispatched, tick + steps, layered ? 1u : 0u, flags};
    const auto lod_pixels = std::bit_cast<std::uint32_t>(listing.lod_pixels);
    const auto faint_floor = std::bit_cast<std::uint32_t>(listing.faint_floor);
    const DrawConstants scatter_parameters = listing.measure ? DrawConstants{counted, layered ? 1u : 0u, records, lod_pixels, faint_floor} :
        DrawConstants{counted, layered ? 1u : 0u};
    const bool lod = listing.lod_pixels > 0 || listing.faint_floor > 0;
    const DrawConstants both_layers = lod ? DrawConstants{records, 0, records, 1, lod_pixels, faint_floor} : DrawConstants{records, 0, records, 1};
    const DrawConstants smoke_layer = lod ? DrawConstants{records, 0, light_first, std::max(offscreen_divisor, 1u), lod_pixels, faint_floor} :
        DrawConstants{records, 0, light_first, std::max(offscreen_divisor, 1u)};
    const DrawConstants light_layer{records, light_first, capacity, 1};
    const DrawConstants every_layer = lod ? DrawConstants{records, 0, records, std::max(offscreen_divisor, 1u), lod_pixels, faint_floor} :
        DrawConstants{records, 0, records, std::max(offscreen_divisor, 1u)};
    const DrawConstants debris_list{capacity, particle_layout::debris_list_first, capacity};
    const auto model = !units ? ParticleModel::legacy : units->aircraft ? ParticleModel::aircraft : ParticleModel::realism;
    const auto& draw = draws_[static_cast<std::size_t>(model)];
    const auto& compute = computes_[static_cast<std::size_t>(model)];
    const auto& particle_pipeline = lod ? *draw.lod_scene : *draw.scene;
    const auto& offscreen_pipeline = lod ? *draw.lod_offscreen : *draw.offscreen;
    const auto& scatter_pipeline = listing.measure ? *compute.scatter_measure : *compute.scatter;
    const bool whole_record = listing.measure || units;
    return {
        {{{*compute.simulation, simulation, parameters, {groups, 1, 1}, "Particle simulation"},
            {*compute.scan, scan, scan_parameters, {1, 1, 1}, "Particle scan"},
            {scatter_pipeline, scatter, scatter_parameters, {groups, 1, 1}, "Particle scatter"}}},
        {particle_pipeline, drawing, both_layers, 0, 0, "Particles", arguments_.get(), particle_layout::particle_arguments_offset},
        {offscreen_pipeline, drawing, smoke_layer, 0, 0, "Smoke", arguments_.get(), particle_layout::smoke_arguments_offset},
        {*draw.scene, drawing, light_layer, 0, 0, "Light", arguments_.get(), particle_layout::light_arguments_offset},
        {offscreen_pipeline, drawing, every_layer, 0, 0, "Smoke and light", arguments_.get(), particle_layout::particle_arguments_offset},
        {*debris_, drawing, debris_list, 0, 0, "Debris", arguments_.get(), particle_layout::debris_arguments_offset},
        {arguments_.get(), particle_layout::counters_offset, whole_record ? particle_layout::counters_bytes : particle_layout::counters_basic_bytes},
        steps, active_effects, units ? units->units : 0};
}

std::optional<ParticleCounters> ParticleSystem::counters(const ReadbackData& data)
{
    static_assert(offsetof(ParticleCounters, measured) == particle_layout::counters_bytes &&
        particle_layout::counters_bytes == particle_layout::arguments_bytes - particle_layout::counters_offset &&
        offsetof(ParticleCounters, offscreen) == particle_layout::counter_offscreen_word * 4 &&
        offsetof(ParticleCounters, smoke_coverage) == particle_layout::counter_smoke_coverage_word * 4 &&
        offsetof(ParticleCounters, light_coverage) == particle_layout::counter_light_coverage_word * 4 &&
        offsetof(ParticleCounters, residual) == realism_layout::counter_residual_word * 4 &&
        offsetof(ParticleCounters, flags) == realism_layout::counter_flags_word * 4 &&
        offsetof(ParticleCounters, ghosts) == realism_layout::counter_ghosts_word * 4);
    if (data.size != particle_layout::counters_basic_bytes && data.size != particle_layout::counters_bytes) return std::nullopt;
    ParticleCounters counters;
    std::memcpy(&counters, data.bytes.data(), data.size);
    counters.measured = data.size == particle_layout::counters_bytes &&
        (counters.flags == 0 || (counters.flags & realism_layout::counter_flag_measured) != 0);
    return counters;
}
}
