#include "explosion_presets.hpp"

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

constexpr float4 hot_emission{4.0f, 2.4f, 0.3f, 0};
constexpr float4 cool_emission{1.8f, 0.03f, 0.003f, 0};

LayerParameters blast_smoke(std::uint32_t count)
{
    LayerParameters layer;
    layer.count = count;
    layer.radius = 1.75f;
    layer.speed_min = 2.5f;
    layer.speed_range = 3.75f;
    layer.up_kick = 3.0f;
    layer.lifetime_min = 6.0f;
    layer.lifetime_range = 2.0f;
    layer.size_min = 1.0f;
    layer.size_range = 1.0f;
    layer.drag = 1.4f;
    layer.buoyancy = 5.0f;
    layer.growth = 0.55f;
    layer.strength = 0.08f;
    layer.fade_in_rate = 4.0f;
    layer.first_color = {0.11f, 0.12f, 0.13f, 0};
    layer.second_color = {0.20f, 0.10f, 0.045f, 0};
    return layer;
}

LayerParameters fireball(std::uint32_t count)
{
    LayerParameters layer;
    layer.count = count;
    layer.radius = 0.625f;
    layer.speed_min = 2.0f;
    layer.speed_range = 6.25f;
    layer.up_kick = 1.25f;
    layer.lifetime_min = 0.35f;
    layer.lifetime_range = 0.45f;
    layer.size_min = 1.0f;
    layer.size_range = 0.625f;
    layer.progress_growth = 1.8f;
    layer.strength = 0.05f;
    layer.first_color = hot_emission;
    layer.second_color = cool_emission;
    return layer;
}

LayerParameters sparks(std::uint32_t count)
{
    LayerParameters layer;
    layer.count = count;
    layer.radius = 0.625f;
    layer.speed_min = 6.0f;
    layer.speed_range = 10.0f;
    layer.up_kick = 4.0f;
    layer.lifetime_min = 1.0f;
    layer.lifetime_range = 1.2f;
    layer.size_min = 0.06f;
    layer.size_range = 0.08f;
    layer.gravity = 9.81f;
    layer.restitution = 0.35f;
    layer.friction = 0.7f;
    layer.strength = 0.6f;
    layer.first_color = hot_emission;
    layer.second_color = cool_emission;
    return layer;
}

std::array<LayerParameters, particle_layout::layer_count> preset_layers(ExplosionPreset preset)
{
    std::array<LayerParameters, particle_layout::layer_count> layers{};
    switch (preset)
    {
    case ExplosionPreset::small_he:
        layers[layer_index(EffectLayer::blast_smoke)] = blast_smoke(512);
        layers[layer_index(EffectLayer::fireball)] = fireball(256);
        layers[layer_index(EffectLayer::sparks)] = sparks(1792);
        break;
    }
    return layers;
}
}

std::string_view preset_name(ExplosionPreset preset)
{
    switch (preset)
    {
    case ExplosionPreset::small_he: return "small-he";
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

EffectClass make_effect_class(ExplosionPreset preset, SurfaceType surface)
{
    static_cast<void>(surface);
    EffectClass effect;
    effect.layers = preset_layers(preset);
    effect.group_layers.fill(static_cast<std::uint8_t>(particle_layout::no_layer));
    std::uint32_t next_group = 0;
    for (std::uint32_t index = 0; index < particle_layout::layer_count; ++index)
    {
        auto& layer = effect.layers[index];
        layer.first_group = next_group;
        layer.groups = (layer.count + particle_layout::simulation_group_size - 1) / particle_layout::simulation_group_size;
        assert(next_group + layer.groups <= particle_layout::slab_groups && "a preset's layers must fit one slab");
        for (std::uint32_t group = 0; group < layer.groups; ++group)
        {
            effect.group_layers[next_group + group] = static_cast<std::uint8_t>(index);
        }
        next_group += layer.groups;
    }
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
    for (const auto& layer : make_effect_class(preset, surface).layers)
    {
        if (layer.count != 0) seconds = std::max(seconds, layer.delay_range + layer.lifetime_min + layer.lifetime_range);
    }
    return static_cast<std::uint32_t>(std::ceil(seconds * 60.0f));
}
}
