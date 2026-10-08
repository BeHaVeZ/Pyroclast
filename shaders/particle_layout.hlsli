// Shared by HLSL and C++ (src/graphics/particle_layout.hpp): only `static const uint` declarations belong here.
#pragma once

static const uint particle_stride = 48;
static const uint particle_position_offset = 0;
static const uint particle_velocity_offset = 16;
static const uint particle_size_offset = 32;
static const uint particle_generation_offset = 36;
static const uint particle_spin_offset = 40;

static const uint explosion_particles = 2560;
static const uint maximum_effects = 128;
static const uint maximum_explosions = 100;

static const uint simulation_group_size = 64;
static const uint maximum_simulation_steps = 4;

static const uint slab_groups = explosion_particles / simulation_group_size;
static const uint scan_threads = 512;
static const uint particle_vertices = 6;
static const uint debris_vertices = 36;
static const uint particle_arguments_offset = 0;
static const uint smoke_arguments_offset = 16;
static const uint light_arguments_offset = 32;
static const uint debris_arguments_offset = 48;
static const uint counters_offset = 64;
static const uint arguments_bytes = 96;
static const uint light_list_first = maximum_effects * explosion_particles;
static const uint debris_list_first = 2 * maximum_effects * explosion_particles;

static const uint layer_ground_dust = 0;
static const uint layer_blast_smoke = 1;
static const uint layer_plume = 2;
static const uint layer_flash = 3;
static const uint layer_fireball = 4;
static const uint layer_sparks = 5;
static const uint layer_debris = 6;
static const uint layer_count = 7;
static const uint no_layer = 255;
static const uint lights_mask_bit = 7;
static const uint curl_mask_bit = 8;
static const uint vortex_mask_bit = 9;
static const uint gusts_mask_bit = 10;
static const uint shading_mask_bit = 11;
static const uint shadow_mask_bit = 12;
static const uint emission_mask_bit = 13;
static const uint props_mask_bit = 14;
static const uint collision_mask_bit = 15;
static const uint deflection_mask_bit = 16;
static const uint full_mask = 0x1ffff;

static const uint spawn_burst = 0;
static const uint spawn_ring = 1;

static const uint maximum_lights = 16;

static const uint maximum_effect_classes = 16;
static const uint class_stride = 2048;
static const uint class_layers_offset = 64;
static const uint layer_stride = 192;
static const uint class_flow_offset = class_layers_offset + layer_count * layer_stride;
