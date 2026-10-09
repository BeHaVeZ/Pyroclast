#pragma once

uint rotor_cycle_hash(uint id, uint cycle, uint salt)
{
    return realism_hash(id ^ realism_hash(cycle ^ salt));
}

float rotor_unit(uint value)
{
    return float(value & 0xffffffu) / 16777216.0;
}

bool simulate_rotor(uint slot, uint local)
{
    uint record = slot * explosion_particles + local;
    uint4 state = frame.effect_states[slot];
    EffectResidual link = load_effect_residual(slot);
    uint tick_before = parameters.tick;
    uint steps = min(parameters.steps, maximum_simulation_steps);
    uint tick_after = tick_before + steps;
    uint mask = frame.light_state.y;
    if (link.rotor_disc) return local < rotor_disc_records && frame.rotor_state.x != 0 && tick_after >= frame.rotor_state.y && tick_after < frame.rotor_state.z;
    if (local < rotor_disc_records) return false;
    uint address = record * particle_stride;
    float4 position = asfloat(particles.Load4(address + particle_position_offset));
    float4 velocity = asfloat(particles.Load4(address + particle_velocity_offset));
    uint4 words = particles.Load4(address + particle_size_offset);
    float size = asfloat(words.x);
    uint generation = words.y;
    uint flags = words.z;
    uint death = words.w;
    if (generation != state.y)
    {
        generation = state.y;
        flags = 0;
        death = 0;
    }
    uint id = state.x * explosion_particles + local;
    uint cycle_length = rotor_cycle_min_ticks + realism_hash(id ^ rotor_salt_cycle) % rotor_cycle_range_ticks;
    uint first = state.z + 1 + realism_hash(id ^ rotor_salt_phase) % cycle_length;
    Layer layer = load_layer(classes, state.w, layer_ground_dust);
    RealismLayer lingering = load_realism_layer(classes, state.w, layer_ground_dust);
    bool props = mask_enabled(mask, props_mask_bit) && mask_enabled(mask, collision_mask_bit);
    Flow none = (Flow)0;
    Lift lift = (Lift)0;
    lift.rise_scale = 1;
    lift.spread_scale = 1;
    lift.direction = float2(1, 0);
    lift.graduation_seconds = float(load_graduation_ticks(classes, state.w)) / 60;
    lift.settling_scale = (local & 1) != 0 ? 2 : 1;
    lift.vortex_scale = 1;
    const float dt = 1.0 / 60.0;
    for (uint step = 0; step < steps; ++step)
    {
        uint tick = tick_before + step + 1;
        if (tick >= first && (tick - first) % cycle_length == 0)
        {
            uint cycle = (tick - first) / cycle_length;
            uint4 births = frame.rotor_births[step];
            death = tick + cycle_length;
            flags = 0;
            if ((rotor_cycle_hash(id, cycle, rotor_salt_birth) & 1023u) >= births.x) continue;
            flags = rotor_flag_born;
            float u = rotor_unit(rotor_cycle_hash(id, cycle, rotor_salt_radius)) * 3;
            float r0 = float(births.y & 0xffffu) / 100;
            float r1 = float(births.y >> 16) / 100;
            float r2 = float(births.z & 0xffffu) / 100;
            float r3 = float(births.z >> 16) / 100;
            float radius = u < 1 ? lerp(r0, r1, u) : u < 2 ? lerp(r1, r2, u - 1) : lerp(r2, r3, u - 2);
            float angle = 6.2831853 * rotor_unit(rotor_cycle_hash(id, cycle, rotor_salt_azimuth));
            float2 ground = frame.rotor_jets[step].zw;
            size = layer.size_min + layer.size_range * rotor_unit(rotor_cycle_hash(id, cycle, rotor_salt_size));
            position.xz = ground + radius * float2(cos(angle), sin(angle));
            position.y = terrain_height(environment, position.xz) + size;
            if (props)
            {
                float3 centre = float3(ground.x, terrain_height(environment, ground) + 1, ground.y);
                position.xyz = prop_clip_spawn(environment, centre, position.xyz);
            }
            bool pad = position.x >= concrete_pad_min_x && position.x <= concrete_pad_max_x && position.z >= concrete_pad_min_z &&
                position.z <= concrete_pad_max_z;
            float edge = 0.5 * float(terrain_resolution - 1) * terrain_spacing;
            if (pad || abs(position.x) > edge || abs(position.z) > edge) flags |= rotor_flag_phantom;
            RotorFlow flow = rotor_flow_at(frame.rotor_hubs[step], frame.rotor_wakes[step], frame.rotor_jets[step]);
            velocity.xyz = 0.6 * rotor_air(flow, position.xyz, position.y - size) +
                float3(0, 1.5 + 1.5 * rotor_unit(rotor_cycle_hash(id, cycle, rotor_salt_kick)), 0);
            velocity.w = lingering_lifetime;
            position.w = 0;
            continue;
        }
        if ((flags & rotor_flag_born) == 0 || tick >= death) continue;
        position.w += dt;
        if (step == 0) velocity.xyz += blast_push(position.xyz, mask, slot, position.w);
        lift.rotor_step = step;
        lingering_step(position, velocity, size, layer, lingering, false, 0, none, float3(0, 0, 0), 0, 1, mask, lift);
    }
    particles.Store4(address + particle_position_offset, asuint(position));
    particles.Store4(address + particle_velocity_offset, asuint(velocity));
    particles.Store4(address + particle_size_offset, uint4(asuint(size), generation, flags, death));
    return (flags & rotor_flag_born) != 0 && tick_after < death && tick_after < link.end && layer_enabled(mask, layer_ground_dust);
}
