#pragma once

#include "../core/math.hpp"
#include "../graphics/particle_layout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace pyroclast
{
enum class SurfaceType : std::uint32_t { dirt, concrete };
inline constexpr std::uint32_t surface_type_count = 2;

enum class ExplosionPreset : std::uint32_t { small_he };
inline constexpr std::uint32_t preset_count = 1;

enum class EffectLayer : std::uint32_t { ground_dust, blast_smoke, plume, flash, fireball, sparks, debris };

struct LayerParameters
{
    std::uint32_t first_group = 0;
    std::uint32_t groups = 0;
    std::uint32_t count = 0;
    std::uint32_t reserved = 0;
    float radius = 0;
    float speed_min = 0;
    float speed_range = 0;
    float up_kick = 0;
    float lifetime_min = 0;
    float lifetime_range = 0;
    float size_min = 0;
    float size_range = 0;
    float delay_range = 0;
    float spread = 1;
    float gravity = 0;
    float drag = 0;
    float buoyancy = 0;
    float restitution = 0;
    float friction = 1;
    float unused = 0;
    float growth = 0;
    float progress_growth = 0;
    float strength = 0;
    float fade_in_rate = 0;
    float4 first_color;
    float4 second_color;
};
static_assert(sizeof(LayerParameters) == particle_layout::layer_stride && offsetof(LayerParameters, radius) == 16 &&
    offsetof(LayerParameters, lifetime_min) == 32 && offsetof(LayerParameters, delay_range) == 48 &&
    offsetof(LayerParameters, buoyancy) == 64 && offsetof(LayerParameters, growth) == 80 &&
    offsetof(LayerParameters, first_color) == 96 && offsetof(LayerParameters, second_color) == 112);

struct EffectClass
{
    std::array<std::uint8_t, particle_layout::class_layers_offset> group_layers{};
    std::array<LayerParameters, particle_layout::layer_count> layers{};
    std::array<std::uint8_t, particle_layout::class_stride - particle_layout::class_layers_offset -
        particle_layout::layer_count * particle_layout::layer_stride> padding{};
};
static_assert(sizeof(EffectClass) == particle_layout::class_stride && particle_layout::slab_groups <= particle_layout::class_layers_offset);

inline constexpr std::string_view preset_names = "small-he";
std::string_view preset_name(ExplosionPreset preset);
std::optional<ExplosionPreset> preset_from_name(std::string_view name);

constexpr std::uint32_t effect_class_index(ExplosionPreset preset, SurfaceType surface)
{
    return static_cast<std::uint32_t>(preset) * surface_type_count + static_cast<std::uint32_t>(surface);
}
inline constexpr std::uint32_t effect_class_count = preset_count * surface_type_count;
static_assert(effect_class_count <= particle_layout::maximum_effect_classes);

EffectClass make_effect_class(ExplosionPreset preset, SurfaceType surface);
std::vector<EffectClass> effect_classes();
std::uint32_t effect_ticks(ExplosionPreset preset, SurfaceType surface);
}
