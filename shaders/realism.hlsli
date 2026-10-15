#pragma once

#include "realism_layout.hlsli"
#include "effect_classes.hlsli"
#include "frame_constants.hlsli"

uint realism_hash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352d;
    value ^= value >> 15;
    value *= 0x846ca68b;
    return value ^ (value >> 16);
}

struct RealismLayer
{
    uint survivor_stride;
    float survivor_area;
    float diffusion_seconds;
    float size_cap;
    float optical_depth;
    float end_share;
    float settling;
    float dilution;
    float residual_drag;
    float residual_turbulence;
    float turbulence_scale;
    uint residual_offset;
    float4 albedo;
};

uint realism_address(uint effect_class)
{
    return effect_class_address(effect_class) + class_realism_offset;
}

uint load_graduation_ticks(ByteAddressBuffer classes, uint effect_class)
{
    return classes.Load(realism_address(effect_class));
}

float4 load_rise(ByteAddressBuffer classes, uint effect_class)
{
    return asfloat(classes.Load4(realism_address(effect_class) + 16));
}

float4 load_spread(ByteAddressBuffer classes, uint effect_class)
{
    return asfloat(classes.Load4(realism_address(effect_class) + 224));
}

float load_late_dilution(ByteAddressBuffer classes, uint effect_class)
{
    return asfloat(classes.Load(realism_address(effect_class) + 12));
}

uint variation_key(uint seed)
{
    return realism_hash(seed ^ 0xa5a5a5a5) & 0xff;
}

uint variation_life(uint seed)
{
    return 888 + realism_hash(seed ^ 0x3c6ef372) % 342;
}

float variation_factor(bool varied, uint key, uint salt, float low, float high)
{
    return varied ? low + (high - low) * float(realism_hash(key ^ salt) & 0xff) / 255 : 1;
}

static const uint vary_height = 0x68e31da4;
static const uint vary_diffusion = 0xb5297a4d;
static const uint vary_darkness = 0x1b56c4e9;
static const uint vary_spread = 0x7c8ba3e1;

struct Rise
{
    float height;
    float2 direction;
    bool varied;
    uint key;
};

uint pack_rise(float height, float2 direction, bool varied, uint key)
{
    float turn = atan2(direction.y, direction.x) * (1 / 6.2831853);
    uint angle = uint(frac(turn) * 128 + 0.5) & 127;
    return f32tof16(height) | (angle << 16) | ((varied ? 1u : 0u) << 23) | ((key & 0xff) << 24);
}

Rise unpack_rise(uint word)
{
    Rise rise;
    rise.height = f16tof32(word & 0xffff);
    float angle = float((word >> 16) & 127) * (6.2831853 / 128);
    rise.direction = float2(cos(angle), sin(angle));
    rise.varied = ((word >> 23) & 1) != 0;
    rise.key = word >> 24;
    return rise;
}

float neutral_height(float4 rise, bool varied, uint key, uint record_id, uint layer_index)
{
    float share = float(realism_hash(record_id ^ 0x2f1a9d3b) & 0xffff) / 65535;
    float fraction;
    if (layer_index == layer_ground_dust) fraction = 0.03 + 0.42 * share * share;
    else if (layer_index == layer_plume) fraction = 0.06 + 0.8 * share;
    else fraction = share < 0.25 ? 0.12 + 1.6 * share : 0.52 + 0.48 * sqrt((share - 0.25) / 0.75);
    return rise.x * variation_factor(varied, key, vary_height, 0.8, 1.25) * fraction;
}

RealismLayer load_realism_layer(ByteAddressBuffer classes, uint effect_class, uint layer_index)
{
    uint address = realism_address(effect_class) + 32 + min(layer_index, layer_plume) * 64;
    uint4 first = classes.Load4(address);
    float4 second = asfloat(classes.Load4(address + 16));
    uint4 third = classes.Load4(address + 32);
    RealismLayer layer;
    layer.survivor_stride = first.x;
    layer.survivor_area = asfloat(first.y);
    layer.diffusion_seconds = asfloat(first.z);
    layer.size_cap = asfloat(first.w);
    layer.optical_depth = second.x;
    layer.end_share = second.y;
    layer.settling = second.z;
    layer.dilution = second.w;
    layer.residual_drag = asfloat(third.x);
    layer.residual_turbulence = asfloat(third.y);
    layer.turbulence_scale = asfloat(third.z);
    layer.residual_offset = third.w;
    layer.albedo = asfloat(classes.Load4(address + 48));
    return layer;
}

struct RealismLook
{
    float3 smoke_soot;
    float young_seconds;
    float3 plume_soot;
    float boost;
    float boost_seconds;
    float jet_speed;
    float vortex_scale;
    float charge_scale;
};

RealismLook load_realism_look(ByteAddressBuffer classes, uint effect_class)
{
    uint address = effect_class_address(effect_class) + class_look_offset;
    float4 first = asfloat(classes.Load4(address));
    float4 second = asfloat(classes.Load4(address + 16));
    RealismLook look;
    look.smoke_soot = first.xyz;
    look.young_seconds = first.w;
    look.plume_soot = second.xyz;
    look.boost = second.w;
    float4 third = asfloat(classes.Load4(address + 32));
    look.boost_seconds = third.x;
    look.jet_speed = third.y;
    look.vortex_scale = third.z;
    look.charge_scale = third.w;
    return look;
}

struct TrailParameters
{
    uint jets_min;
    uint jets_range;
    float head_speed;
    float tilt_min;
    float tilt_range;
    uint trail_ticks;
    float knot_share;
    float hang_seconds;
    float knot_soot;
    float knot_density;
};

TrailParameters load_trail(ByteAddressBuffer classes, uint effect_class)
{
    uint address = effect_class_address(effect_class) + class_layers_offset + layer_blast_smoke * layer_stride + layer_realism_offset;
    uint4 first = classes.Load4(address);
    uint4 second = classes.Load4(address + 16);
    uint2 third = classes.Load2(address + 32);
    TrailParameters trail;
    trail.jets_min = first.x;
    trail.jets_range = first.y;
    trail.head_speed = asfloat(first.z);
    trail.tilt_min = asfloat(first.w);
    trail.tilt_range = asfloat(second.x);
    trail.trail_ticks = second.y;
    trail.knot_share = asfloat(second.z);
    trail.hang_seconds = asfloat(second.w);
    trail.knot_soot = asfloat(third.x);
    trail.knot_density = asfloat(third.y);
    return trail;
}

struct ClodParameters
{
    float size_exponent;
    float chunk_share;
    float chunk_boost;
    float life_floor;
    float lead_size;
    float lead_life;
};

ClodParameters load_clods(ByteAddressBuffer classes, uint effect_class)
{
    uint address = effect_class_address(effect_class) + class_layers_offset + layer_debris * layer_stride + layer_realism_offset;
    float4 first = asfloat(classes.Load4(address));
    float2 second = asfloat(classes.Load2(address + 16));
    ClodParameters clods;
    clods.size_exponent = first.x;
    clods.chunk_share = first.y;
    clods.chunk_boost = first.z;
    clods.life_floor = first.w;
    clods.lead_size = second.x;
    clods.lead_life = second.y;
    return clods;
}

bool jet_record(RealismLook look, uint layer_index, uint slab_index)
{
    return look.jet_speed > 0 && layer_index == layer_blast_smoke && ((slab_index >> 3) & 3) == 1;
}

float macro_shadow(float3 settled, float3 centre, float radius, float density)
{
    float3 offset = settled - centre;
    float b = dot(offset, frame.sun_direction.xyz);
    float c = dot(offset, offset) - radius * radius;
    float disc = b * b - c;
    if (disc <= 0) return 1;
    float root = sqrt(disc);
    float chord = max(-b + root - max(-b - root, 0), 0);
    return exp(-density * chord / max(radius, 1e-3));
}

float column_shadow(ByteAddressBuffer classes, uint effect_class, float3 origin, float2 drift, float3 position, float neutral, bool varied,
    uint key)
{
    float height = load_rise(classes, effect_class).x * variation_factor(varied, key, vary_height, 0.8, 1.25);
    float3 centre = float3(origin.x + drift.x, origin.y + 0.6 * height, origin.z + drift.y);
    return macro_shadow(float3(position.x, neutral, position.z), centre, 0.5 * height, load_spread(classes, effect_class).w);
}

float henyey_greenstein(float g, float cosine)
{
    return (1 - g * g) / pow(max(1 + g * g - 2 * g * cosine, 1e-4), 1.5);
}

static const float realism_light_cap = 6;

float3 realism_point_lights(float3 position)
{
    float3 total = 0;
    uint count = min(frame.light_state.x, maximum_lights);
    for (uint index = 0; index < count; ++index)
    {
        float3 offset = frame.light_positions[index].xyz - position;
        float distance_squared = dot(offset, offset);
        float radius = frame.light_positions[index].w;
        float source = frame.light_colors[index].w;
        float window = saturate(1 - distance_squared / (radius * radius));
        total += frame.light_colors[index].rgb * (window * window / max(source * source + distance_squared, 1));
    }
    float brightest = max(max(total.r, total.g), max(total.b, 1e-6));
    return total * min(1, realism_light_cap / brightest);
}

struct Unit
{
    uint id;
    uint first_group;
};

uint unit_word(uint index)
{
    return frame.unit_order[index / 4][index % 4];
}

Unit find_unit(uint group)
{
    uint low = 0;
    uint high = max(min(frame.unit_state.x, maximum_units), 1);
    [loop] while (high - low > 1)
    {
        uint middle = (low + high) / 2;
        if ((unit_word(middle) >> unit_id_bits) <= group) low = middle;
        else high = middle;
    }
    uint word = unit_word(low);
    Unit unit;
    unit.id = word & unit_id_mask;
    unit.first_group = word >> unit_id_bits;
    return unit;
}

bool unit_is_cluster(Unit unit)
{
    return unit.id >= unit_cluster_first;
}

struct Cluster
{
    uint ring_group;
    uint records;
    uint fade_ticks;
    uint tag;
    uint fade_end;
};

Cluster load_cluster(uint index)
{
    uint4 pair = frame.clusters[min(index, maximum_clusters - 1) / 2];
    bool second = (index & 1) != 0;
    uint packed = second ? pair.z : pair.x;
    Cluster cluster;
    cluster.ring_group = packed & ((1u << ring_group_bits) - 1);
    cluster.records = (packed >> record_count_shift) & ((1u << record_count_bits) - 1);
    cluster.fade_ticks = (((packed >> fade_code_shift) & 7) + 1) * fade_unit_ticks;
    cluster.tag = packed >> tag_shift;
    cluster.fade_end = second ? pair.w : pair.y;
    return cluster;
}

struct EffectResidual
{
    uint ring_group;
    uint cluster;
    uint tag;
    uint graduation_tick;
    uint longest_ticks;
    bool linger;
    bool varied;
    bool skirt;
    float2 drift;
#ifdef PYROCLAST_ROTOR
    bool rotor;
    bool rotor_disc;
    bool jet;
    uint end;
#endif
};

EffectResidual load_effect_residual(uint slot)
{
    uint4 words = frame.effect_residual[min(slot, maximum_effects - 1)];
    EffectResidual link;
    link.ring_group = words.x & ((1u << ring_group_bits) - 1);
    link.cluster = (words.x >> effect_cluster_shift) & ((1u << (effect_tag_shift - effect_cluster_shift)) - 1);
    link.tag = (words.x >> effect_tag_shift) & ((1u << tag_bits) - 1);
    link.graduation_tick = words.y;
    link.longest_ticks = words.z & ((1u << effect_longest_bits) - 1);
    link.linger = (words.z & latched_linger) != 0;
    link.varied = (words.z & latched_variation) != 0;
    link.skirt = (words.z & latched_skirt) != 0;
    link.drift = float2(f16tof32(words.w & 0xffff), f16tof32(words.w >> 16));
#ifdef PYROCLAST_ROTOR
    link.rotor = (words.z & latched_rotor) != 0;
    link.rotor_disc = link.rotor && words.x == 1;
    link.jet = link.rotor && words.x == 2;
    link.end = words.w;
#endif
    return link;
}

uint residual_death_tick(uint start_tick, uint graduation_tick, uint longest_ticks, uint variation, uint jitter)
{
    uint longest = (longest_ticks * variation) >> 10;
    uint graduated = graduation_tick - start_tick;
    uint after = longest > graduated ? longest - graduated : 0;
    uint life = after > jitter ? after - jitter : 0;
    return graduation_tick + max(crossfade_ticks + 60, life);
}

uint pack_residual(uint effect_class, uint layer_index, uint cluster, float shadow, uint tag)
{
    return effect_class | (layer_index << packed_layer_shift) | (cluster << packed_cluster_shift) |
        (uint(saturate(shadow) * 255 + 0.5) << packed_shadow_shift) | (tag << tag_shift);
}

uint packed_class(uint packed)
{
    return packed & 0xf;
}

uint packed_layer(uint packed)
{
    return (packed >> packed_layer_shift) & 3;
}

uint packed_cluster(uint packed)
{
    return (packed >> packed_cluster_shift) & ((1u << (packed_shadow_shift - packed_cluster_shift)) - 1);
}

float packed_shadow(uint packed)
{
    return float((packed >> packed_shadow_shift) & 0xff) / 255;
}

uint packed_tag(uint packed)
{
    return packed >> tag_shift;
}

bool lingering_record(EffectResidual link, RealismLayer lingering, uint layer_index)
{
    return link.linger && layer_index <= layer_plume && lingering.survivor_stride != 0;
}

float3 lingering_wind(float height, uint mask)
{
    if (!mask_enabled(mask, shear_mask_bit) || height <= 15) return frame.wind.xyz * (0.35 + 0.65 * saturate(height / 15));
    float strength = pow(min(height, 200.0) / 15, 0.2);
    float veer = 0.34906585 * saturate((height - 15) / 200);
    float s = sin(veer);
    float c = cos(veer);
    return float3(frame.wind.x * c - frame.wind.z * s, frame.wind.y, frame.wind.x * s + frame.wind.z * c) * strength;
}

float cloud_shadow(Layer layer, Flow flow, float3 origin, float3 position, float seconds)
{
    if (flow.cloud_radius <= 0 || layer.shadow_density <= 0) return 1;
    float2 ring = ring_at(flow, seconds);
    float3 centre = origin + float3(0, ring.x, 0);
    float radius = (ring.y + flow.core_radius) * flow.cloud_radius;
    float3 offset = position - centre;
    float along = dot(offset, frame.sun_direction.xyz);
    float discriminant = along * along - (dot(offset, offset) - radius * radius);
    if (discriminant <= 0) return 1;
    float root = sqrt(discriminant);
    float chord = max(-along + root, 0) - max(-along - root, 0);
    return exp(-layer.shadow_density * chord);
}
