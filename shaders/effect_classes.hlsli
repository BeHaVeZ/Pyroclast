#pragma once

#include "particle_layout.hlsli"

struct Layer
{
    uint first_group;
    uint groups;
    uint count;
    uint spawn;
    float radius;
    float speed_min;
    float speed_range;
    float up_kick;
    float lifetime_min;
    float lifetime_range;
    float size_min;
    float size_range;
    float delay_range;
    float spread;
    float gravity;
    float drag;
    float buoyancy;
    float restitution;
    float friction;
    float stretch;
    float growth;
    float progress_growth;
    float strength;
    float fade_in_rate;
    float3 first_color;
    float3 second_color;
};

uint effect_class_address(uint effect_class)
{
    return min(effect_class, maximum_effect_classes - 1) * class_stride;
}

uint group_layer(ByteAddressBuffer classes, uint effect_class, uint group)
{
    uint word = classes.Load(effect_class_address(effect_class) + (min(group, slab_groups - 1) / 4) * 4);
    return (word >> ((group % 4) * 8)) & 0xff;
}

Layer load_layer(ByteAddressBuffer classes, uint effect_class, uint layer_index)
{
    uint address = effect_class_address(effect_class) + class_layers_offset + min(layer_index, layer_count - 1) * layer_stride;
    uint4 layout = classes.Load4(address);
    float4 spawn = asfloat(classes.Load4(address + 16));
    float4 lifetime = asfloat(classes.Load4(address + 32));
    float4 motion = asfloat(classes.Load4(address + 48));
    float4 contact = asfloat(classes.Load4(address + 64));
    float4 look = asfloat(classes.Load4(address + 80));
    Layer layer;
    layer.first_group = layout.x;
    layer.groups = layout.y;
    layer.count = layout.z;
    layer.spawn = layout.w;
    layer.radius = spawn.x;
    layer.speed_min = spawn.y;
    layer.speed_range = spawn.z;
    layer.up_kick = spawn.w;
    layer.lifetime_min = lifetime.x;
    layer.lifetime_range = lifetime.y;
    layer.size_min = lifetime.z;
    layer.size_range = lifetime.w;
    layer.delay_range = motion.x;
    layer.spread = motion.y;
    layer.gravity = motion.z;
    layer.drag = motion.w;
    layer.buoyancy = contact.x;
    layer.restitution = contact.y;
    layer.friction = contact.z;
    layer.stretch = contact.w;
    layer.growth = look.x;
    layer.progress_growth = look.y;
    layer.strength = look.z;
    layer.fade_in_rate = look.w;
    layer.first_color = asfloat(classes.Load3(address + 96));
    layer.second_color = asfloat(classes.Load3(address + 112));
    return layer;
}

bool smoke_like(uint layer_index)
{
    return layer_index < layer_flash;
}

bool layer_enabled(uint mask, uint layer_index)
{
    return layer_index < layer_count && ((mask >> layer_index) & 1) != 0;
}
