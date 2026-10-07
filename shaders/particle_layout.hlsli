// Shared by HLSL and C++ (src/graphics/particle_layout.hpp): only `static const uint` declarations belong here.
#pragma once

static const uint particle_stride = 48;
static const uint particle_position_offset = 0;
static const uint particle_velocity_offset = 16;
static const uint particle_size_offset = 32;
static const uint particle_generation_offset = 36;

static const uint fireball_particles = 256;
static const uint emissive_particles = 2048;
static const uint smoke_particles = 512;
static const uint explosion_particles = emissive_particles + smoke_particles;
static const uint maximum_effects = 128;
static const uint maximum_explosions = 100;

static const uint simulation_group_size = 64;
static const uint maximum_simulation_steps = 4;

static const uint slab_groups = explosion_particles / simulation_group_size;
static const uint smoke_groups = smoke_particles / simulation_group_size;
static const uint scan_threads = 512;
static const uint particle_vertices = 6;
static const uint particle_arguments_offset = 0;
static const uint smoke_arguments_offset = 16;
static const uint light_arguments_offset = 32;
static const uint counters_offset = 48;
static const uint arguments_bytes = 64;
static const uint light_list_first = maximum_effects * smoke_particles;
