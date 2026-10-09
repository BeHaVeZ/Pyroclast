#pragma once

#include "../core/math.hpp"
#include "../graphics/particle_layout.hpp"
#include "../graphics/realism_layout.hpp"

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

enum class ExplosionPreset : std::uint32_t
{
    small_impact,
    small_he,
    artillery_he,
    heavy,
    fuel_fire,
    dust_blast,
    stress_test
};
inline constexpr std::uint32_t preset_count = 7;

enum class EffectLayer : std::uint32_t { ground_dust, blast_smoke, plume, flash, fireball, sparks, debris };
inline constexpr std::uint32_t layer_count = particle_layout::layer_count;

struct LayerParameters
{
    std::uint32_t first_group = 0;
    std::uint32_t groups = 0;
    std::uint32_t count = 0;
    std::uint32_t spawn = particle_layout::spawn_burst;
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
    float stretch = 0;
    float growth = 0;
    float progress_growth = 0;
    float strength = 0;
    float fade_in_rate = 0;
    float4 first_color;
    float4 second_color;
    float cooling = 0;
    float heat = 0;
    float expansion = 0;
    float turbulence = 0;
    float emission = 0;
    float shadow_density = 0;
    std::array<float, 10> reserved_smoke{};
};
static_assert(sizeof(LayerParameters) == particle_layout::layer_stride && offsetof(LayerParameters, radius) == 16 &&
    offsetof(LayerParameters, lifetime_min) == 32 && offsetof(LayerParameters, delay_range) == 48 &&
    offsetof(LayerParameters, buoyancy) == 64 && offsetof(LayerParameters, growth) == 80 &&
    offsetof(LayerParameters, first_color) == 96 && offsetof(LayerParameters, second_color) == 112 &&
    offsetof(LayerParameters, cooling) == 128 && offsetof(LayerParameters, emission) == 144);

struct FlowParameters
{
    float strength = 0;
    float rise_height = 0;
    float rise_time = 1;
    float ring_radius = 0;
    float ring_growth = 0;
    float core_radius = 1;
    float decay_time = 1;
    float cloud_radius = 0;
};
static_assert(sizeof(FlowParameters) == 32);

struct RealismLayer
{
    std::uint32_t survivor_stride = 0;
    float survivor_area = 1;
    float diffusion_seconds = 1;
    float size_cap = 0;
    float optical_depth = 0;
    float end_share = 0.3f;
    float settling = 0;
    float dilution = 2;
    float residual_drag = 0;
    float residual_turbulence = 0;
    float turbulence_scale = 0.04f;
    std::uint32_t residual_offset = 0;
    float4 albedo{};
};
static_assert(sizeof(RealismLayer) == 64);

struct RealismParameters
{
    std::uint32_t graduation_ticks = 0;
    std::uint32_t residual_records = 0;
    std::uint32_t residual_groups = 0;
    std::uint32_t reserved = 0;
    float4 rise{};
    std::array<RealismLayer, 3> layers{};
    float4 spread{};
};
static_assert(sizeof(RealismParameters) == 240 && offsetof(RealismParameters, layers) == 32 && offsetof(RealismParameters, spread) == 224);

struct EventVariation
{
    std::uint32_t key = 0;
    std::uint32_t life = 1024;
};
EventVariation event_variation(std::uint32_t seed);
std::uint32_t particle_hash(std::uint32_t value);
float charge_kilograms(ExplosionPreset preset);
float stabilization_height(ExplosionPreset preset);

struct EffectClass
{
    std::array<std::uint8_t, particle_layout::class_layers_offset> group_layers{};
    std::array<LayerParameters, particle_layout::layer_count> layers{};
    FlowParameters flow;
    RealismParameters realism;
    std::array<std::uint8_t, particle_layout::class_stride - realism_layout::class_realism_offset - sizeof(RealismParameters)> padding{};
};
static_assert(sizeof(EffectClass) == particle_layout::class_stride && offsetof(EffectClass, flow) == particle_layout::class_flow_offset &&
    offsetof(EffectClass, realism) == realism_layout::class_realism_offset &&
    particle_layout::slab_groups <= particle_layout::class_layers_offset);

inline constexpr std::string_view preset_names = "small-impact, small-he, artillery-he, heavy, fuel-fire, dust-blast, stress-test";
std::string_view preset_name(ExplosionPreset preset);
std::optional<ExplosionPreset> preset_from_name(std::string_view name);
std::string_view surface_name(SurfaceType surface);
std::string_view layer_name(EffectLayer layer);

constexpr std::uint32_t effect_class_index(ExplosionPreset preset, SurfaceType surface)
{
    return static_cast<std::uint32_t>(preset) * surface_type_count + static_cast<std::uint32_t>(surface);
}
inline constexpr std::uint32_t effect_class_count = preset_count * surface_type_count;
static_assert(effect_class_count <= particle_layout::maximum_effect_classes);

EffectClass make_effect_class(ExplosionPreset preset, SurfaceType surface);
std::vector<EffectClass> effect_classes();

float cloud_rise(ExplosionPreset preset, float seconds);
std::uint32_t realism_effect_ticks(ExplosionPreset preset, SurfaceType surface);
std::uint32_t realism_early_ticks(ExplosionPreset preset, SurfaceType surface);
std::uint32_t effect_ticks(ExplosionPreset preset, SurfaceType surface);

struct LightCurve
{
    float flash_intensity = 0;
    float flash_seconds = 0;
    float glow_intensity = 0;
    float glow_seconds = 0;
    float3 color{1, 1, 1};
    float radius = 0;
    float height = 0;
};
LightCurve light_curve(ExplosionPreset preset);
float light_intensity(const LightCurve& curve, float seconds);
}
