// Shared by HLSL and C++ (src/graphics/realism_layout.hpp): only `static const uint` declarations belong here.
#pragma once

#include "particle_layout.hlsli"

static const uint realism_mask_bit = 17;
static const uint linger_mask_bit = 18;
static const uint rise_mask_bit = 19;
static const uint shear_mask_bit = 20;
static const uint variation_mask_bit = 21;
static const uint haze_mask_bit = 22;
static const uint waves_mask_bit = 23;
static const uint skirt_mask_bit = 24;
static const uint condensation_mask_bit = 25;
static const uint push_mask_bit = 26;
static const uint shake_mask_bit = 27;
static const uint rotor_mask_bit = 28;
static const uint brownout_mask_bit = 29;
// realism_full_mask lives only in realism_layout.hpp: declaring its value here would reorder the SPIR-V constants of
// shaders that use the same number.

static const uint residual_groups = 1024;
static const uint residual_records = residual_groups * simulation_group_size;
static const uint residual_first = maximum_effects * explosion_particles;
static const uint pool_records = residual_first + residual_records;
static const uint maximum_clusters = 384;

static const uint maximum_units = maximum_effects + maximum_clusters;
static const uint maximum_unit_groups = maximum_effects * slab_groups + residual_groups;
static const uint unit_cluster_first = maximum_effects;
static const uint unit_id_bits = 9;
static const uint unit_id_mask = (1u << unit_id_bits) - 1;

// clusters, two words each: first ring group (0-10), record count (11-19), fade code (20-22), tag (23-31); fade end tick.
static const uint ring_group_bits = 11;
static const uint record_count_shift = 11;
static const uint record_count_bits = 9;
static const uint fade_code_shift = 20;
static const uint fade_unit_ticks = 120;
static const uint tag_shift = 23;
static const uint tag_bits = 9;
static const uint no_fade = 0xffffffff;

// effect_residual: x ring group (0-10), cluster (11-19), tag (20-28); y graduation tick; z longest life (0-14), latched
// bits (15 linger, 16 variation, 17 skirt); w drift x, z as halves. Rotor slots: {0 dust or 1 discs, ~0, latched_rotor, end}.
static const uint effect_cluster_shift = 11;
static const uint effect_tag_shift = 20;
static const uint effect_longest_bits = 15;
static const uint latched_linger = 1u << 15;
static const uint latched_variation = 1u << 16;
static const uint latched_skirt = 1u << 17;
static const uint latched_rotor = 1u << 18;
static const uint scale_one = 1024;

// A residual record: float3 position, age | float3 velocity, age at graduation | spawn size, packed (class 0-3, layer
// 4-5, cluster 6-14, sun share 15-22 in 1/255, tag 23-31) | death tick | rise (half 0-15, direction 16-22, varied 23, key 24-31).
static const uint residual_packed_offset = 36;
static const uint residual_death_offset = 40;
static const uint residual_rise_offset = 44;
static const uint packed_layer_shift = 4;
static const uint packed_cluster_shift = 6;
static const uint packed_shadow_shift = 15;

static const uint crossfade_ticks = 60;

static const uint counter_residual_word = 13;
static const uint counter_flags_word = 14;
static const uint counter_ghosts_word = 15;
static const uint counter_flag_measured = 1;
static const uint counter_flag_realism = 2;

static const uint class_realism_offset = 1440;

static const uint class_blast_offset = 1680;
static const uint class_look_offset = 2000;
static const uint layer_realism_offset = 152;
static const uint blast_knots = 16;
static const uint maximum_blasts = 16;
static const uint push_return_ticks = 15;
