#pragma once

static const float jet_mass = 12000.0f;
static const float jet_span = 10.0f;
static const float jet_vortex_spacing = 7.853982f;
static const float jet_nozzle_aft = 7.4f;
static const float jet_nozzle_radius = 0.45f;

static const uint jet_knot_ticks = 6;
static const uint jet_knot_limit = 160;
static const float jet_window = 30.0f;
static const float jet_body_window = 25.0f;
static const float jet_body_radius = 2.2f;
static const float jet_speed_limit = 40.0f;
static const float jet_turbulence = 0.2f;
static const float jet_turbulence_scale = 2.0f;
static const float jet_shred_reach = 1.2f;
static const float jet_shred_start = 1.5f;
static const float jet_shred_full = 6.0f;

static const float jet_sound_speed = 340.3f;
static const float jet_dry_thrust = 60000.0f;
static const float jet_wet_thrust = 110000.0f;
static const float jet_exhaust_excess = 600.0f;
static const float jet_haze_seconds = 0.8f;
static const float jet_haze_angle = 0.000025f;
static const float jet_vapor_mach = 0.8f;
static const float jet_vapor_humidity = 0.6f;
static const float jet_humidity_per_kelvin = 0.06f;
static const float jet_air_heat_capacity = 1005.0f;
static const float jet_condensation_humidity = 0.95f;
static const float jet_condensation_seconds = 1.5f;

static const uint jet_exhaust_records = 1280;
static const uint jet_cycle_min_ticks = 300;
static const uint jet_cycle_range_ticks = 421;
static const uint jet_salt_cycle = 0x68e31da4u;
static const uint jet_salt_phase = 0xb5297a4du;
static const uint jet_salt_birth = 0x1b56c4e9u;
static const uint jet_salt_place = 0x7fb5d329u;
static const uint jet_salt_side = 0xd2a98b26u;
static const uint jet_salt_size = 0x3c2b1a0fu;
static const uint jet_salt_kick = 0x94d049bbu;
static const uint jet_exhaust_emission = 1024;
static const float jet_exhaust_kick = 8.0f;
static const float jet_saltation = 4.0f;
static const float jet_reference_flux = 75000.0f;
