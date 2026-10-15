#pragma once

bool simulate_jet(uint slot, uint local)
{
    uint record = slot * explosion_particles + local;
    uint4 state = frame.effect_states[slot];
    EffectResidual link = load_effect_residual(slot);
    uint tick_before = parameters.tick;
    uint steps = min(parameters.steps, maximum_simulation_steps);
    uint tick_after = tick_before + steps;
    uint mask = frame.light_state.y;
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
    uint cycle_length = jet_cycle_min_ticks + realism_hash(id ^ jet_salt_cycle) % jet_cycle_range_ticks;
    uint first = state.z + 1 + realism_hash(id ^ jet_salt_phase) % cycle_length;
    bool exhaust = local < jet_exhaust_records;
    uint layer_index = exhaust ? layer_blast_smoke : layer_ground_dust;
    Layer layer = load_layer(classes, state.w, layer_index);
    RealismLayer lingering = load_realism_layer(classes, state.w, layer_index);
    bool props = mask_enabled(mask, props_mask_bit) && mask_enabled(mask, collision_mask_bit);
    Flow none = (Flow)0;
    Lift lift = (Lift)0;
    lift.rise_scale = 1;
    lift.spread_scale = 1;
    lift.direction = float2(1, 0);
    lift.graduation_seconds = float(load_graduation_ticks(classes, state.w)) / 60;
    lift.settling_scale = exhaust ? 0 : ((local & 1) != 0 ? 2 : 1);
    lift.vortex_scale = 1;
    const float dt = 1.0 / 60.0;
    for (uint step = 0; step < steps; ++step)
    {
        uint tick = tick_before + step + 1;
        if (tick >= first && (tick - first) % cycle_length == 0)
        {
            uint cycle = (tick - first) / cycle_length;
            uint4 births = frame.jet_births[step];
            death = tick + cycle_length;
            flags = 0;
            uint emission = exhaust ? births.x : births.y;
            if ((rotor_cycle_hash(id, cycle, jet_salt_birth) & 1023u) >= emission) continue;
            flags = rotor_flag_born;
            size = layer.size_min + layer.size_range * rotor_unit(rotor_cycle_hash(id, cycle, jet_salt_size));
            float place = rotor_unit(rotor_cycle_hash(id, cycle, jet_salt_place));
            float kick = rotor_unit(rotor_cycle_hash(id, cycle, jet_salt_kick));
            if (exhaust)
            {
                float3 centre = frame.jet_heads[step * 4 + 2].xyz;
                float3 moving = frame.jet_heads[step * 4 + 3].xyz;
                float3 forward = moving / max(length(moving), 1e-3);
                float3 nozzle = centre - forward * jet_nozzle_aft;
                float3 jitter = float3(kick - 0.5, rotor_unit(rotor_cycle_hash(id, cycle, jet_salt_side)) - 0.5, frac(kick * 7.31) - 0.5);
                position.xyz = nozzle - moving * (place / 60.0) + jitter * 0.6;
                velocity.xyz = -forward * jet_exhaust_kick;
            }
            else
            {
                float4 dust_line = frame.jet_dust_lines[step];
                float2 ground = lerp(dust_line.xy, dust_line.zw, place);
                float2 along = dust_line.zw - dust_line.xy;
                float reach = length(along);
                along = reach > 1e-3 ? along / reach : float2(1, 0);
                float2 across = float2(-along.y, along.x);
                float side = (rotor_cycle_hash(id, cycle, jet_salt_side) & 1u) != 0 ? 1 : -1;
                float half_width = float(births.z) / 100;
                position.xz = ground + across * (side * half_width * (0.6 + 0.8 * kick));
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
                velocity.xyz = float3(across.x, 0, across.y) * (side * (2 + 3 * kick)) + float3(0, 1 + place, 0);
            }
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
    return (flags & rotor_flag_born) != 0 && tick_after < death && tick_after < link.end && layer_enabled(mask, layer_index);
}
