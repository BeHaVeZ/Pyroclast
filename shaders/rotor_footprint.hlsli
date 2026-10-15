#pragma once

bool rotor_record(uint record)
{
    return record < residual_first && load_effect_residual(record / explosion_particles).rotor;
}

RealismFootprint jet_footprint(ByteAddressBuffer particles, ByteAddressBuffer classes, uint record)
{
    RealismFootprint result = (RealismFootprint)0;
    uint slot = record / explosion_particles;
    uint local = record % explosion_particles;
    uint4 state = frame.effect_states[slot];
    uint layer_index = local < jet_exhaust_records ? layer_blast_smoke : layer_ground_dust;
    result.realism = true;
    result.residual = true;
    result.slot = slot;
    result.effect_class = state.w;
    result.layer_index = layer_index;
    result.survivor_index = local;
    result.stretch = 1;
    result.stretch_axis = float2(1, 0);
    result.shadow = 1;
    uint address = record * particle_stride;
    float4 position = asfloat(particles.Load4(address + particle_position_offset));
    uint4 words = particles.Load4(address + particle_size_offset);
    float spawn_size = asfloat(words.x);
    uint flags = words.z;
    uint death = words.w;
    Layer layer = load_layer(classes, state.w, layer_index);
    RealismLayer lingering = load_realism_layer(classes, state.w, layer_index);
    float age = max(position.w, 0);
    float size = min(spawn_size * sqrt(1 + age / max(lingering.diffusion_seconds, 1e-3)), lingering.size_cap);
    float depth = lingering.optical_depth * saturate(age * layer.fade_in_rate) * pow(saturate(spawn_size / max(size, 1e-4)), lingering.dilution);
    uint id = state.x * explosion_particles + local;
    uint cycle_length = jet_cycle_min_ticks + realism_hash(id ^ jet_salt_cycle) % jet_cycle_range_ticks;
    uint clock = frame.clock.x;
    float left = float(death > clock ? death - clock : 0);
    depth *= smoothstep(0, 1, left / (lingering.end_share * float(cycle_length)));
    if ((flags & rotor_flag_born) == 0 || (flags & rotor_flag_phantom) != 0) depth = 0;
    float3 jet = jet_draw_air(position.xyz, size);
    result.age = age;
    result.id = id;
    result.spawn_size = spawn_size;
    result.pattern = realism_hash(id ^ 0x2545f491u);
    result.albedo = layer.first_color.rgb;
    result.softness = max(size, 0.1);
    Footprint footprint;
    footprint.trail_pixels = 0;
    footprint.position = position.xyz;
    footprint.distance = max(mul(frame.view_projection, float4(position.xyz, 1)).w, frame.camera_position.w);
    footprint.pixels = size * frame.projection.x / footprint.distance;
    footprint.flat = footprint.pixels > 0 && footprint.pixels < 0.5;
    if (footprint.flat) size *= 0.5 / footprint.pixels;
    footprint.size = size;
    footprint.bound = 1.4142136 * size + footprint.distance / max(frame.projection.x, 1e-6);
    float coverage = footprint.flat ? 1.5707963 * footprint.pixels * footprint.pixels : 1;
    footprint.opacity = (1 - exp(-depth)) * coverage;
    result.footprint = footprint;
    rotor_stretch(result, jet + rotor_draw_air(position.xyz, 0));
    return result;
}

RealismFootprint rotor_footprint(ByteAddressBuffer particles, ByteAddressBuffer classes, uint record)
{
    if (load_effect_residual(record / explosion_particles).jet) return jet_footprint(particles, classes, record);
    RealismFootprint result = (RealismFootprint)0;
    uint slot = record / explosion_particles;
    uint local = record % explosion_particles;
    uint4 state = frame.effect_states[slot];
    result.realism = true;
    result.residual = true;
    result.slot = slot;
    result.effect_class = state.w;
    result.layer_index = layer_ground_dust;
    result.survivor_index = local;
    result.stretch = 1;
    result.stretch_axis = float2(1, 0);
    result.shadow = 1;
    Footprint footprint;
    footprint.trail_pixels = 0;
    if (load_effect_residual(slot).rotor_disc)
    {
        uint disc = min(local, rotor_disc_records - 1);
        float4 centre = frame.rotor_discs[disc * 3];
        float blades = float(rotor_blades) * (disc == 0 ? rotor_chord : tail_rotor_chord);
        footprint.position = centre.xyz;
        footprint.size = max(centre.w, 0.01);
        footprint.distance = max(mul(frame.view_projection, float4(centre.xyz, 1)).w, frame.camera_position.w);
        footprint.pixels = footprint.size * frame.projection.x / footprint.distance;
        footprint.flat = false;
        footprint.bound = 1.4142136 * footprint.size + footprint.distance / max(frame.projection.x, 1e-6);
        footprint.opacity = saturate(blades / (rotor_pi * footprint.size)) * asfloat(frame.rotor_state.w);
        result.footprint = footprint;
        result.softness = 0.1;
        return result;
    }
    uint address = record * particle_stride;
    float4 position = asfloat(particles.Load4(address + particle_position_offset));
    uint4 words = particles.Load4(address + particle_size_offset);
    float spawn_size = asfloat(words.x);
    uint flags = words.z;
    uint death = words.w;
    Layer layer = load_layer(classes, state.w, layer_ground_dust);
    RealismLayer lingering = load_realism_layer(classes, state.w, layer_ground_dust);
    float age = max(position.w, 0);
    float size = min(spawn_size * sqrt(1 + age / max(lingering.diffusion_seconds, 1e-3)), lingering.size_cap);
    float depth = lingering.optical_depth * saturate(age * layer.fade_in_rate) * pow(saturate(spawn_size / max(size, 1e-4)), lingering.dilution);
    float graduation_seconds = float(load_graduation_ticks(classes, state.w)) / 60;
    depth *= lerp(0.3, 1, exp(-max(age - graduation_seconds, 0) / 20));
    uint id = state.x * explosion_particles + local;
    uint cycle_length = rotor_cycle_min_ticks + realism_hash(id ^ rotor_salt_cycle) % rotor_cycle_range_ticks;
    uint clock = frame.clock.x;
    float left = float(death > clock ? death - clock : 0);
    depth *= smoothstep(0, 1, left / (lingering.end_share * float(cycle_length)));
    if ((flags & rotor_flag_born) == 0 || (flags & rotor_flag_phantom) != 0) depth = 0;
    result.age = age;
    result.id = id;
    result.spawn_size = spawn_size;
    result.pattern = realism_hash(id ^ 0x2545f491u);
    result.albedo = layer.first_color.rgb;
    float4 sphere = frame.rotor_dust_shadow[0];
    float macro = sphere.w > 0 ? macro_shadow(position.xyz, sphere.xyz, sphere.w, frame.rotor_dust_shadow[1].x) : 1;
    result.shadow = pow(lerp(1, macro, saturate(age / 2)), max(frame.residual.z, 1));
    result.softness = max(size, 0.1);
    footprint.position = position.xyz;
    footprint.distance = max(mul(frame.view_projection, float4(position.xyz, 1)).w, frame.camera_position.w);
    footprint.pixels = size * frame.projection.x / footprint.distance;
    footprint.flat = footprint.pixels > 0 && footprint.pixels < 0.5;
    if (footprint.flat) size *= 0.5 / footprint.pixels;
    footprint.size = size;
    footprint.bound = 1.4142136 * size + footprint.distance / max(frame.projection.x, 1e-6);
    float coverage = footprint.flat ? 1.5707963 * footprint.pixels * footprint.pixels : 1;
    footprint.opacity = (1 - exp(-depth)) * coverage;
    result.footprint = footprint;
    rotor_stretch(result, rotor_draw_air(position.xyz, 0));
    return result;
}
