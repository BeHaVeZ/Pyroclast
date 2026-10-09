#pragma once

#include "particle_layout.hlsli"

static const uint realism_mask_bit = 17;
static const uint linger_mask_bit = 18;
static const uint rise_mask_bit = 19;
static const uint shear_mask_bit = 20;
static const uint variation_mask_bit = 21;
static const uint haze_mask_bit = 22;
static const uint realism_full_mask = 0x7fffff;

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

static const uint ring_group_bits = 11;
static const uint record_count_shift = 11;
static const uint record_count_bits = 9;
static const uint fade_code_shift = 20;
static const uint fade_unit_ticks = 120;
static const uint tag_shift = 23;
static const uint tag_bits = 9;
static const uint no_fade = 0xffffffff;

static const uint effect_cluster_shift = 11;
static const uint effect_tag_shift = 20;
static const uint effect_longest_bits = 15;
static const uint latched_linger = 1u << 15;
static const uint latched_variation = 1u << 16;
static const uint scale_one = 1024;

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
