// Shared by HLSL and C++ (src/graphics/particle_layout.hpp): only `static const uint` declarations belong here.

static const uint particle_stride = 32;
static const uint particle_motion_offset = 0;
static const uint particle_life_offset = 16;
static const uint particle_epoch_offset = 28;

static const uint fireball_particles = 256;
static const uint emissive_particles = 2048;
static const uint smoke_particles = 512;
static const uint explosion_particles = emissive_particles + smoke_particles;

static const uint simulation_group_size = 64;
static const uint maximum_simulation_steps = 4;
