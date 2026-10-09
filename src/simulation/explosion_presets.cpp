#include "explosion_presets.hpp"
#include "blast_front.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace pyroclast
{
namespace
{
constexpr std::uint32_t layer_index(EffectLayer layer)
{
    return static_cast<std::uint32_t>(layer);
}

constexpr std::uint32_t groups_for(std::uint32_t count)
{
    return (count + particle_layout::simulation_group_size - 1) / particle_layout::simulation_group_size;
}

constexpr float4 hot_emission{4.0f, 2.4f, 0.3f, 0};
constexpr float4 cool_emission{1.8f, 0.03f, 0.003f, 0};
constexpr float4 cold_smoke{0.30f, 0.30f, 0.31f, 0};
constexpr float4 hot_smoke{0.24f, 0.16f, 0.10f, 0};
constexpr float4 sooty_smoke{0.13f, 0.12f, 0.115f, 0};

LayerParameters ground_dust(std::uint32_t count, float reach, float size)
{
    LayerParameters layer;
    layer.count = count;
    layer.spawn = particle_layout::spawn_ring;
    layer.radius = 0.15f * reach;
    layer.speed_min = 0.8f * reach;
    layer.speed_range = 0.7f * reach;
    layer.lifetime_min = 3.5f;
    layer.lifetime_range = 2.5f;
    layer.size_min = 0.6f * size;
    layer.size_range = 0.8f * size;
    layer.drag = 2.2f;
    layer.buoyancy = 0.6f;
    layer.growth = 0.45f;
    layer.strength = 0.11f;
    layer.fade_in_rate = 6.0f;
    layer.cooling = 2.0f;
    layer.heat = 0.1f;
    layer.expansion = 0.3f;
    layer.turbulence = 0.8f;
    layer.shadow_density = 0.05f;
    return layer;
}

LayerParameters blast_smoke(std::uint32_t count, float scale, float4 cold = cold_smoke)
{
    LayerParameters layer;
    layer.count = count;
    layer.radius = 1.75f * scale;
    layer.speed_min = 2.5f * scale;
    layer.speed_range = 3.75f * scale;
    layer.up_kick = 3.0f * scale;
    layer.lifetime_min = 6.0f;
    layer.lifetime_range = 2.0f;
    layer.size_min = 1.0f * scale;
    layer.size_range = 1.0f * scale;
    layer.drag = 1.4f;
    layer.buoyancy = 5.0f;
    layer.growth = 0.4f;
    layer.strength = 0.11f;
    layer.fade_in_rate = 4.0f;
    layer.first_color = cold;
    layer.second_color = hot_smoke;
    layer.cooling = 0.45f;
    layer.heat = 1.0f;
    layer.expansion = 0.6f;
    layer.turbulence = 1.2f;
    layer.emission = 0.6f;
    layer.shadow_density = 0.12f;
    return layer;
}

LayerParameters plume(std::uint32_t count, float scale, float feed_seconds, float4 cold = cold_smoke)
{
    LayerParameters layer;
    layer.count = count;
    layer.radius = 1.2f * scale;
    layer.spread = 0.3f;
    layer.speed_min = 1.5f * scale;
    layer.speed_range = 2.0f * scale;
    layer.up_kick = 2.0f * scale;
    layer.lifetime_min = 5.0f;
    layer.lifetime_range = 3.0f;
    layer.size_min = 1.0f * scale;
    layer.size_range = 0.8f * scale;
    layer.delay_range = feed_seconds;
    layer.drag = 0.9f;
    layer.buoyancy = 7.0f;
    layer.growth = 0.35f;
    layer.strength = 0.08f;
    layer.fade_in_rate = 2.0f;
    layer.first_color = cold;
    layer.second_color = hot_smoke;
    layer.cooling = 0.25f;
    layer.heat = 0.9f;
    layer.expansion = 0.5f;
    layer.turbulence = 1.5f;
    layer.emission = 0.4f;
    layer.shadow_density = 0.12f;
    return layer;
}

LayerParameters flash(float size)
{
    LayerParameters layer;
    layer.count = 4;
    layer.lifetime_min = 0.06f;
    layer.lifetime_range = 0.05f;
    layer.size_min = size;
    layer.size_range = 0.3f * size;
    layer.progress_growth = 0.6f;
    layer.strength = 0.5f;
    layer.first_color = {12.0f, 10.0f, 7.0f, 0};
    layer.second_color = {6.0f, 3.0f, 1.0f, 0};
    return layer;
}

LayerParameters fireball(std::uint32_t count, float scale, float burn = 1.0f, float strength = 0.05f, float4 hot = hot_emission)
{
    LayerParameters layer;
    layer.count = count;
    layer.radius = 0.625f * scale;
    layer.speed_min = 2.0f * scale;
    layer.speed_range = 6.25f * scale;
    layer.up_kick = 1.25f * scale;
    layer.lifetime_min = 0.35f * burn;
    layer.lifetime_range = 0.45f * burn;
    layer.size_min = 1.0f * scale;
    layer.size_range = 0.625f * scale;
    layer.progress_growth = 1.8f;
    layer.strength = strength;
    layer.first_color = hot;
    layer.second_color = cool_emission;
    return layer;
}

LayerParameters sparks(std::uint32_t count, float speed)
{
    LayerParameters layer;
    layer.count = count;
    layer.radius = 0.625f;
    layer.speed_min = 6.0f * speed;
    layer.speed_range = 10.0f * speed;
    layer.up_kick = 4.0f;
    layer.lifetime_min = 1.0f;
    layer.lifetime_range = 1.2f;
    layer.size_min = 0.06f;
    layer.size_range = 0.08f;
    layer.gravity = 9.81f;
    layer.restitution = 0.35f;
    layer.friction = 0.7f;
    layer.stretch = 0.035f;
    layer.strength = 0.6f;
    layer.first_color = hot_emission;
    layer.second_color = cool_emission;
    return layer;
}

LayerParameters debris(std::uint32_t count, float speed, float size)
{
    LayerParameters layer;
    layer.count = count;
    layer.radius = 0.5f;
    layer.spread = 0.75f;
    layer.speed_min = 5.0f * speed;
    layer.speed_range = 8.0f * speed;
    layer.up_kick = 3.0f;
    layer.lifetime_min = 4.0f;
    layer.lifetime_range = 3.0f;
    layer.size_min = 0.12f * size;
    layer.size_range = 0.22f * size;
    layer.gravity = 9.81f;
    layer.restitution = 0.3f;
    layer.friction = 0.55f;
    layer.strength = 1.0f;
    return layer;
}

using Layers = std::array<LayerParameters, particle_layout::layer_count>;

Layers preset_layers(ExplosionPreset preset)
{
    Layers layers{};
    auto& dust = layers[layer_index(EffectLayer::ground_dust)];
    auto& smoke = layers[layer_index(EffectLayer::blast_smoke)];
    auto& column = layers[layer_index(EffectLayer::plume)];
    auto& light = layers[layer_index(EffectLayer::flash)];
    auto& fire = layers[layer_index(EffectLayer::fireball)];
    auto& spark = layers[layer_index(EffectLayer::sparks)];
    auto& fragments = layers[layer_index(EffectLayer::debris)];
    switch (preset)
    {
    case ExplosionPreset::small_impact:
        dust = ground_dust(128, 4, 0.6f);
        smoke = blast_smoke(128, 0.5f);
        light = flash(1.5f);
        fire = fireball(64, 0.4f, 0.6f);
        spark = sparks(256, 0.7f);
        fragments = debris(64, 0.6f, 0.6f);
        break;
    case ExplosionPreset::small_he:
        dust = ground_dust(256, 7, 1.0f);
        smoke = blast_smoke(512, 1);
        light = flash(3);
        fire = fireball(256, 1);
        spark = sparks(768, 1);
        fragments = debris(128, 1, 1);
        break;
    case ExplosionPreset::artillery_he:
        dust = ground_dust(384, 11, 1.6f);
        smoke = blast_smoke(768, 1.5f);
        column = plume(256, 1.3f, 2.0f);
        light = flash(5);
        fire = fireball(384, 1.5f, 1.2f);
        spark = sparks(512, 1.3f);
        fragments = debris(192, 1.3f, 1.4f);
        break;
    case ExplosionPreset::heavy:
        dust = ground_dust(384, 15, 2.2f);
        smoke = blast_smoke(896, 2.2f);
        column = plume(384, 2.0f, 3.0f);
        light = flash(8);
        fire = fireball(512, 2.2f, 1.5f);
        spark = sparks(192, 1.6f);
        fragments = debris(128, 1.6f, 2.0f);
        break;
    case ExplosionPreset::fuel_fire:
        dust = ground_dust(64, 5, 1.0f);
        smoke = blast_smoke(384, 1.6f, sooty_smoke);
        column = plume(640, 1.6f, 4.0f, sooty_smoke);
        light = flash(6);
        fire = fireball(1024, 2.0f, 2.5f, 0.018f, {3.0f, 1.3f, 0.2f, 0});
        spark = sparks(128, 0.8f);
        break;
    case ExplosionPreset::dust_blast:
        dust = ground_dust(1024, 14, 2.0f);
        smoke = blast_smoke(448, 1.2f);
        light = flash(3);
        fire = fireball(128, 0.8f, 0.8f);
        spark = sparks(128, 0.8f);
        fragments = debris(384, 1.2f, 1.3f);
        break;
    case ExplosionPreset::stress_test:
        dust = ground_dust(384, 12, 1.8f);
        smoke = blast_smoke(768, 1.8f);
        column = plume(384, 1.6f, 3.0f);
        light = flash(6);
        fire = fireball(512, 1.8f, 1.3f);
        spark = sparks(320, 1.4f);
        fragments = debris(128, 1.4f, 1.5f);
        break;
    }
    return layers;
}

FlowParameters preset_flow(ExplosionPreset preset)
{
    switch (preset)
    {
    case ExplosionPreset::small_impact: return {};
    case ExplosionPreset::small_he: return {3.5f, 7, 1.5f, 1.5f, 2.5f, 2.5f, 2.0f, 1.2f};
    case ExplosionPreset::artillery_he: return {9, 14, 2.2f, 2.5f, 5, 4.5f, 2.5f, 1.2f};
    case ExplosionPreset::heavy: return {20, 26, 3.0f, 4, 9, 7.0f, 3.5f, 1.2f};
    case ExplosionPreset::fuel_fire: return {15, 20, 2.8f, 3, 6, 5.0f, 3.5f, 1.3f};
    case ExplosionPreset::dust_blast: return {4, 8, 2.0f, 3, 5, 5.0f, 2.0f, 1.3f};
    case ExplosionPreset::stress_test: return {12, 18, 2.5f, 3, 6, 5.0f, 3.0f, 1.2f};
    }
    return {};
}

}

float cloud_rise(ExplosionPreset preset, float seconds)
{
    const auto flow = preset_flow(preset);
    return flow.rise_height * (1 - std::exp(-seconds / std::max(flow.rise_time, 1e-3f)));
}

namespace
{
void apply_surface(Layers& layers, SurfaceType surface)
{
    auto& dust = layers[layer_index(EffectLayer::ground_dust)];
    auto& spark = layers[layer_index(EffectLayer::sparks)];
    auto& fragments = layers[layer_index(EffectLayer::debris)];
    const auto times = [](std::uint32_t count, float factor) {
        return static_cast<std::uint32_t>(std::lround(static_cast<float>(count) * factor));
    };
    if (surface == SurfaceType::dirt)
    {
        dust.first_color = {0.50f, 0.41f, 0.29f, 0};
        dust.second_color = {0.55f, 0.40f, 0.25f, 0};
        fragments.first_color = {0.07f, 0.05f, 0.035f, 0};
        spark.count = times(spark.count, 0.6f);
        return;
    }
    dust.count = times(dust.count, 0.6f);
    dust.first_color = {0.66f, 0.65f, 0.62f, 0};
    dust.second_color = {0.68f, 0.63f, 0.56f, 0};
    fragments.count = times(fragments.count, 1.25f);
    fragments.size_min *= 0.7f;
    fragments.size_range *= 0.7f;
    fragments.first_color = {0.33f, 0.32f, 0.30f, 0};
    spark.count = times(spark.count, 1.5f);
}

std::uint32_t total_groups(const Layers& layers)
{
    std::uint32_t groups = 0;
    for (const auto& layer : layers) groups += groups_for(layer.count);
    return groups;
}

void fit_slab(Layers& layers)
{
    while (total_groups(layers) > particle_layout::slab_groups)
    {
        auto largest = std::max_element(layers.begin(), layers.end(), [](const auto& a, const auto& b) { return a.count < b.count; });
        largest->count = (groups_for(largest->count) - 1) * particle_layout::simulation_group_size;
    }
}
}

namespace
{
std::uint32_t graduation_seconds(ExplosionPreset preset)
{
    switch (preset)
    {
    case ExplosionPreset::small_impact: return 4;
    case ExplosionPreset::small_he: return 6;
    case ExplosionPreset::artillery_he: return 8;
    case ExplosionPreset::heavy: return 10;
    case ExplosionPreset::fuel_fire: return 10;
    case ExplosionPreset::dust_blast: return 6;
    case ExplosionPreset::stress_test: return 8;
    }
    return 6;
}

constexpr std::uint32_t survivor_stride = 8;
constexpr float survivor_area = 2.8284271f;
constexpr float diffusion_seconds = 4;
constexpr float size_cap_factor = 2.89f;

constexpr float4 lingering_smoke{0.13f, 0.125f, 0.12f, 1};
constexpr float4 lingering_plume{0.10f, 0.095f, 0.09f, 1};
constexpr float4 lingering_soot{0.07f, 0.066f, 0.062f, 1};

RealismParameters realism_parameters(ExplosionPreset preset, const Layers& layers)
{
    RealismParameters realism;
    realism.graduation_ticks = graduation_seconds(preset) * 60;
    const float height = stabilization_height(preset);
    realism.rise = {height, 0.06f * height, 25, 0.05f};
    realism.spread = {0.03f * height, 0.25f * height, 20, 0};
    std::uint32_t records = 0;
    for (std::uint32_t index = 0; index < realism.layers.size(); ++index)
    {
        const auto& layer = layers[index];
        auto& lingering = realism.layers[index];
        if (layer.count == 0) continue;
        lingering.survivor_stride = survivor_stride;
        lingering.survivor_area = survivor_area;
        lingering.diffusion_seconds = diffusion_seconds;
        lingering.size_cap = size_cap_factor * (layer.size_min + 0.5f * layer.size_range);
        lingering.optical_depth = -std::log(1 - layer.strength);
        lingering.residual_drag = layer.drag;
        lingering.residual_turbulence = 0.5f * layer.turbulence;
        lingering.residual_offset = records;
        const bool sooty = preset == ExplosionPreset::fuel_fire;
        if (index == layer_index(EffectLayer::ground_dust))
        {
            lingering.albedo = {0.85f * layer.first_color.x, 0.85f * layer.first_color.y, 0.85f * layer.first_color.z, 1};
            lingering.settling = 0.3f;
        }
        else lingering.albedo = sooty ? lingering_soot : index == layer_index(EffectLayer::plume) ? lingering_plume : lingering_smoke;
        records += (layer.count + survivor_stride - 1) / survivor_stride;
    }
    realism.residual_records = records;
    realism.residual_groups = groups_for(records);
    return realism;
}

BlastParameters blast_parameters(ExplosionPreset preset)
{
    BlastParameters blast;
    const float kilograms = charge_kilograms(preset);
    const float scale = std::cbrt(kilograms);
    blast.knots = make_blast_knots(kilograms);
    blast.wave = {4.0f, 0.3f, 0.03f, 0};
    blast.skirt = {4 * scale, 10 * scale, 0.3f, 2.0f};
    if (preset == ExplosionPreset::heavy) blast.wilson = {0.6f, 1, 15, 5 * scale};
    else if (preset == ExplosionPreset::stress_test) blast.wilson = {0.5f, 1, 12, 5 * scale};
    blast.wave_ticks = (blast.knots.back().arrival_q16 + 0xffffu) >> 16;
    blast.charge_kilograms = kilograms;
    blast.push_share = 0.1f;
    blast.push_cap = 30;
    return blast;
}
}

std::string_view preset_name(ExplosionPreset preset)
{
    switch (preset)
    {
    case ExplosionPreset::small_impact: return "small-impact";
    case ExplosionPreset::small_he: return "small-he";
    case ExplosionPreset::artillery_he: return "artillery-he";
    case ExplosionPreset::heavy: return "heavy";
    case ExplosionPreset::fuel_fire: return "fuel-fire";
    case ExplosionPreset::dust_blast: return "dust-blast";
    case ExplosionPreset::stress_test: return "stress-test";
    }
    return "unknown";
}

std::optional<ExplosionPreset> preset_from_name(std::string_view name)
{
    for (std::uint32_t index = 0; index < preset_count; ++index)
    {
        const auto preset = static_cast<ExplosionPreset>(index);
        if (preset_name(preset) == name) return preset;
    }
    return std::nullopt;
}

std::string_view surface_name(SurfaceType surface)
{
    return surface == SurfaceType::concrete ? "concrete" : "dirt";
}

std::string_view layer_name(EffectLayer layer)
{
    switch (layer)
    {
    case EffectLayer::ground_dust: return "dust";
    case EffectLayer::blast_smoke: return "smoke";
    case EffectLayer::plume: return "plume";
    case EffectLayer::flash: return "flash";
    case EffectLayer::fireball: return "fireball";
    case EffectLayer::sparks: return "sparks";
    case EffectLayer::debris: return "debris";
    }
    return "unknown";
}

namespace
{
EffectClass layered_effect_class(ExplosionPreset preset, SurfaceType surface)
{
    EffectClass effect;
    effect.layers = preset_layers(preset);
    effect.flow = preset_flow(preset);
    apply_surface(effect.layers, surface);
    fit_slab(effect.layers);
    effect.realism = realism_parameters(preset, effect.layers);
    effect.group_layers.fill(static_cast<std::uint8_t>(particle_layout::no_layer));
    std::uint32_t next_group = 0;
    for (std::uint32_t index = 0; index < particle_layout::layer_count; ++index)
    {
        auto& layer = effect.layers[index];
        layer.first_group = next_group;
        layer.groups = groups_for(layer.count);
        assert(next_group + layer.groups <= particle_layout::slab_groups && "a preset's layers must fit one slab");
        for (std::uint32_t group = 0; group < layer.groups; ++group)
        {
            effect.group_layers[next_group + group] = static_cast<std::uint8_t>(index);
        }
        next_group += layer.groups;
    }
    return effect;
}
}

EffectClass make_effect_class(ExplosionPreset preset, SurfaceType surface)
{
    auto effect = layered_effect_class(preset, surface);
    effect.blast = blast_parameters(preset);
    return effect;
}

std::vector<EffectClass> effect_classes()
{
    std::vector<EffectClass> classes;
    classes.reserve(effect_class_count);
    for (std::uint32_t preset = 0; preset < preset_count; ++preset)
    {
        for (std::uint32_t surface = 0; surface < surface_type_count; ++surface)
        {
            classes.push_back(make_effect_class(static_cast<ExplosionPreset>(preset), static_cast<SurfaceType>(surface)));
        }
    }
    return classes;
}

std::uint32_t effect_ticks(ExplosionPreset preset, SurfaceType surface)
{
    float seconds = 0;
    for (const auto& layer : layered_effect_class(preset, surface).layers)
    {
        if (layer.count != 0) seconds = std::max(seconds, layer.delay_range + layer.lifetime_min + layer.lifetime_range);
    }
    return static_cast<std::uint32_t>(std::ceil(seconds * 60.0f));
}

std::uint32_t realism_effect_ticks(ExplosionPreset preset, SurfaceType surface)
{
    const auto effect = layered_effect_class(preset, surface);
    std::uint32_t ticks = effect.realism.graduation_ticks + realism_layout::crossfade_ticks;
    for (std::uint32_t index = 0; index < effect.layers.size(); ++index)
    {
        const auto& layer = effect.layers[index];
        const bool lingers = index < effect.realism.layers.size() && effect.realism.layers[index].survivor_stride != 0;
        if (layer.count == 0 || lingers) continue;
        ticks = std::max(ticks, static_cast<std::uint32_t>(std::ceil((layer.delay_range + layer.lifetime_min + layer.lifetime_range) * 60.0f)));
    }
    return ticks;
}

std::uint32_t particle_hash(std::uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

EventVariation event_variation(std::uint32_t seed)
{
    return {particle_hash(seed ^ 0xa5a5a5a5u) & 0xffu, 888 + particle_hash(seed ^ 0x3c6ef372u) % 342};
}

float charge_kilograms(ExplosionPreset preset)
{
    switch (preset)
    {
    case ExplosionPreset::small_impact: return 0.1f;
    case ExplosionPreset::small_he: return 1;
    case ExplosionPreset::artillery_he: return 7;
    case ExplosionPreset::heavy: return 50;
    case ExplosionPreset::fuel_fire: return 5;
    case ExplosionPreset::dust_blast: return 10;
    case ExplosionPreset::stress_test: return 15;
    }
    return 1;
}

float stabilization_height(ExplosionPreset preset)
{
    if (preset == ExplosionPreset::fuel_fire) return 100;
    if (preset == ExplosionPreset::dust_blast) return 20;
    return 45 * std::pow(charge_kilograms(preset) / 7, 0.25f);
}

std::uint32_t realism_early_ticks(ExplosionPreset preset, SurfaceType surface)
{
    const auto effect = layered_effect_class(preset, surface);
    std::uint32_t early = 0;
    for (std::uint32_t index = 0; index < effect.layers.size(); ++index)
    {
        const auto& layer = effect.layers[index];
        if (layer.count == 0) continue;
        const bool lingers = index < effect.realism.layers.size() && effect.realism.layers[index].survivor_stride != 0;
        const float seconds = lingers ? layer.delay_range + 1 / std::max(layer.fade_in_rate, 1e-3f) :
            layer.delay_range + layer.lifetime_min + layer.lifetime_range;
        early = std::max(early, static_cast<std::uint32_t>(std::ceil(seconds * 60.0f)) + (lingers ? realism_layout::crossfade_ticks : 0u));
    }
    return early;
}

LightCurve light_curve(ExplosionPreset preset)
{
    constexpr float3 fire{1.0f, 0.55f, 0.22f};
    switch (preset)
    {
    case ExplosionPreset::small_impact: return {120, 0.08f, 20, 0.4f, fire, 12, 1.0f};
    case ExplosionPreset::small_he: return {600, 0.12f, 120, 0.9f, fire, 30, 2.0f};
    case ExplosionPreset::artillery_he: return {1500, 0.15f, 300, 1.3f, fire, 45, 3.0f};
    case ExplosionPreset::heavy: return {3500, 0.2f, 700, 1.8f, fire, 70, 5.0f};
    case ExplosionPreset::fuel_fire: return {1200, 0.15f, 900, 3.5f, {1.0f, 0.45f, 0.15f}, 55, 4.0f};
    case ExplosionPreset::dust_blast: return {500, 0.1f, 80, 0.8f, fire, 30, 2.0f};
    case ExplosionPreset::stress_test: return {2000, 0.15f, 400, 1.5f, fire, 50, 3.0f};
    }
    return {};
}

float light_intensity(const LightCurve& curve, float seconds)
{
    const auto ease = [seconds](float intensity, float duration) {
        if (duration <= 0 || seconds < 0 || seconds >= duration) return 0.0f;
        const float left = 1 - seconds / duration;
        return intensity * left * left;
    };
    return ease(curve.flash_intensity, curve.flash_seconds) + ease(curve.glow_intensity, curve.glow_seconds);
}
}
