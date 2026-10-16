// Shared by HLSL and C++ (src/simulation/terrain.hpp): only `static const` scalar declarations belong here.
#pragma once

static const uint terrain_resolution = 257;
static const float terrain_spacing = 2.0f;
static const float concrete_pad_min_x = 18.0f;
static const float concrete_pad_max_x = 54.0f;
static const float concrete_pad_min_z = -18.0f;
static const float concrete_pad_max_z = 18.0f;

static const uint environment_grid_offset = (terrain_resolution * terrain_resolution * 4 + 15) / 16 * 16;
static const uint environment_grid_cells = 32;
static const uint environment_cell_bytes = 16;
static const float environment_grid_min = -64.0f;
static const float environment_cell_size = 4.0f;
// Exact, so the CPU and the GPU put every point in the same cell.
static const float environment_cell_inverse = 0.25f;
static const uint environment_boxes_offset =
    environment_grid_offset + environment_grid_cells * environment_grid_cells * environment_cell_bytes;
static const uint maximum_prop_boxes = 64;
static const uint prop_box_stride = 32;
static const uint environment_flags_offset = environment_boxes_offset + maximum_prop_boxes * prop_box_stride;
static const uint environment_bytes = environment_flags_offset + 16;
static const float environment_reach = 3.0f;
// With the furthest move per step, keeps environment_reach sufficient at both ends of a step (environment_tests.cpp).
static const float prop_contact_limit = 1.0f;
static const float prop_step_limit = 2.0f;
static const uint prop_concrete = 0;
static const uint prop_rock = 1;
static const uint prop_wood = 2;
static const uint prop_metal = 3;
static const uint prop_vertex_stride = 32;
static const float prop_spawn_backoff = 0.05f;
static const float deflection_band = 1.5f;
static const float deflection_lift = 0.5f;
