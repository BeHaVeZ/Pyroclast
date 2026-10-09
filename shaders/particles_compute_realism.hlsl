#define PYROCLAST_REALISM
#include "particles_compute.hlsl"
#include "realism.hlsli"

RWByteAddressBuffer arguments : register(u2);

static const float lingering_speed_limit = 114;
static const float lingering_lifetime = 1e9;

void lingering_step(inout float4 position, inout float4 velocity, float size, Layer layer, RealismLayer lingering, bool ring,
    float vortex_weight, Flow flow, float3 effect_origin, float seconds, float crossfade, uint mask)
{
    const float dt = 1.0 / 60.0;
    bool props = mask_enabled(mask, props_mask_bit) && mask_enabled(mask, collision_mask_bit);
    bool deflect = props && mask_enabled(mask, deflection_mask_bit);
    bool curl = mask_enabled(mask, curl_mask_bit);
    float radius = min(size, prop_contact_limit);
    float terrain = terrain_height(environment, position.xz);
    uint2 near = props ? nearby_boxes(environment, position.xyz, !ring) : uint2(0, 0);
    float3 air = wind_at(position.y - terrain);
    if (vortex_weight > 0 && flow.strength > 0 && mask_enabled(mask, vortex_mask_bit))
    {
        air += vortex_flow(flow, effect_origin, position.xyz, seconds) * vortex_weight;
    }
    if (curl && crossfade < 1 && layer.turbulence > 0)
    {
        air += curl_noise(position.xyz * 0.125 - float3(0, seconds * 0.2, 0)) * (layer.turbulence * exp(-0.3 * position.w) * (1 - crossfade));
    }
    if (curl && crossfade > 0 && lingering.residual_turbulence > 0)
    {
        air += curl_noise(position.xyz * lingering.turbulence_scale - float3(0, position.w * 0.05, 0)) * (lingering.residual_turbulence * crossfade);
    }
    if (deflect && any(near != 0)) air = deflect_air(environment, near, position.xyz, radius, air);
    velocity.xyz += (air - velocity.xyz) * (lerp(layer.drag, lingering.residual_drag, crossfade) * dt);
    if (layer.buoyancy > 0) velocity.y += layer.buoyancy * layer_temperature(layer, position.w) * dt;
    float speed_squared = dot(velocity.xyz, velocity.xyz);
    if (speed_squared > lingering_speed_limit * lingering_speed_limit) velocity.xyz *= lingering_speed_limit * rsqrt(speed_squared);
    float3 start = position.xyz;
    position.xyz += velocity.xyz * dt;
    terrain = terrain_height(environment, position.xz);
    float ground = terrain + size;
    if (position.y < ground)
    {
        position.y = ground;
        velocity.y = max(velocity.y, 0);
    }
    float support = terrain;
    if (props)
    {
        uint2 open_bottom = load_open_bottoms(environment);
        uint2 boxes = near | nearby_boxes(environment, position.xyz, !ring);
        for (uint word = 0; word < 2; ++word)
        {
            uint bits = word == 0 ? boxes.x : boxes.y;
            uint open = word == 0 ? open_bottom.x : open_bottom.y;
            [loop] while (bits != 0)
            {
                uint bit = firstbitlow(bits);
                bits &= bits - 1;
                PropBox box = load_prop_box(environment, word * 32 + bit);
                prop_contact(box, ((open >> bit) & 1) != 0, start, position.xyz, velocity.xyz, radius, layer.restitution, layer.friction);
                if (ring) support = max(support, prop_support(box, position.xyz, radius));
            }
        }
    }
    if (!ring)
    {
        float clearance = position.y - terrain;
        if (clearance < 2 * size) velocity.y += (2 * size - clearance) * 1.5 * dt;
    }
    if (ring) position.y = min(position.y, support + size + 0.3 * position.w);
}

bool simulate_lingering(uint slot, uint local, uint record, uint layer_index, Layer layer, RealismLayer lingering, EffectResidual link)
{
    uint4 state = frame.effect_states[slot];
    uint index = local - layer.first_group * simulation_group_size;
    bool survivor = index % lingering.survivor_stride == 0;
    uint graduation = link.graduation_tick;
    uint tick_before = parameters.tick;
    if (tick_before >= graduation) return false;
    uint address = record * particle_stride;
    float4 position = asfloat(particles.Load4(address + particle_position_offset));
    float4 velocity = asfloat(particles.Load4(address + particle_velocity_offset));
    float size = asfloat(particles.Load(address + particle_size_offset));
    uint generation = particles.Load(address + particle_generation_offset);
    uint mask = frame.light_state.y;
    bool props = mask_enabled(mask, props_mask_bit) && mask_enabled(mask, collision_mask_bit);
    bool ring = layer.spawn == spawn_ring;
    if (generation != state.y)
    {
        generation = state.y;
        float3 origin = frame.effect_origins[slot].xyz;
        float3 normal = frame.effect_normals[slot].xyz;
        if (ring && props && normal.y < 0.7)
        {
            normal = float3(0, 1, 0);
            origin.y = prop_floor(environment, origin) + 0.5;
        }
        uint id = state.x * explosion_particles + local;
        float3 local_direction = particle_direction(id);
        if (ring)
        {
            float angle = particle_random(id, 8) * 6.2831853;
            local_direction = normalize(float3(cos(angle), 0.06 + 0.1 * particle_random(id, 9), sin(angle)));
        }
        else if (layer.spread < 1)
        {
            local_direction = normalize(float3(local_direction.x * layer.spread, local_direction.y, local_direction.z * layer.spread));
        }
        float3 direction = around_normal(local_direction, normal);
        position.xyz = origin + direction * particle_random(id, 3) * layer.radius;
        if (props) position.xyz = prop_clip_spawn(environment, origin, position.xyz);
        velocity.xyz = direction * (layer.speed_min + particle_random(id, 2) * layer.speed_range) + float3(0, layer.up_kick, 0);
        velocity.w = lingering_lifetime;
        size = layer.size_min + particle_random(id, 5) * layer.size_range;
        position.w = 0;
        if (layer.delay_range > 0) position.w = -particle_random(id, 7) * layer.delay_range;
    }
    Flow flow = load_flow(classes, state.w);
    float3 effect_origin = frame.effect_origins[slot].xyz;
    bool graduated = false;
    float graduation_age = 0;
    float shadow = 1;
    const float dt = 1.0 / 60.0;
    for (uint step = 0; step < min(parameters.steps, maximum_simulation_steps); ++step)
    {
        uint tick = tick_before + step + 1;
        position.w += dt;
        if (position.w < 0) continue;
        float seconds = float(tick - state.z) / 60.0;
        float vortex_weight = tick < graduation ? saturate(float(graduation - tick) / crossfade_ticks) : 0;
        lingering_step(position, velocity, size, layer, lingering, ring, vortex_weight, flow, effect_origin, seconds, 1 - vortex_weight, mask);
        if (tick == graduation)
        {
            graduated = true;
            graduation_age = position.w;
            if (mask_enabled(mask, shading_mask_bit) && mask_enabled(mask, shadow_mask_bit))
            {
                shadow = cloud_shadow(layer, flow, effect_origin, position.xyz, seconds);
            }
        }
    }
    particles.Store4(address + particle_position_offset, asuint(position));
    particles.Store4(address + particle_velocity_offset, asuint(velocity));
    particles.Store3(address + particle_size_offset, uint3(asuint(size), generation, 0));
    if (survivor && graduated)
    {
        uint id = state.x * explosion_particles + local;
        uint jitter = particle_hash(id ^ 0x9e3779b9) % 301;
        uint death = residual_death_tick(state.z, graduation, link.longest_ticks, scale_one, jitter);
        uint residual = residual_first + link.ring_group * simulation_group_size + lingering.residual_offset + index / lingering.survivor_stride;
        if (residual < min(parameters.record_count, pool_records))
        {
            uint target = residual * particle_stride;
            particles.Store4(target + particle_position_offset, asuint(position));
            particles.Store4(target + particle_velocity_offset, asuint(float4(velocity.xyz, graduation_age)));
            particles.Store4(target + particle_size_offset, uint4(asuint(size), pack_residual(state.w, layer_index, link.cluster, shadow, link.tag),
                death, 0));
        }
    }
    uint tick_after = tick_before + min(parameters.steps, maximum_simulation_steps);
    bool alive = tick_after < graduation || (survivor && graduated);
    return alive && position.w >= 0 && layer_enabled(mask, layer_index);
}

bool simulate_residual(uint cluster_index, uint group_in_cluster, uint thread)
{
    Cluster cluster = load_cluster(cluster_index);
    uint index = group_in_cluster * simulation_group_size + thread;
    uint record = residual_first + (cluster.ring_group + group_in_cluster) * simulation_group_size + thread;
    if (index >= cluster.records || record >= min(parameters.record_count, pool_records)) return false;
    uint address = record * particle_stride;
    uint3 words = particles.Load3(address + particle_size_offset + 4);
    uint packed = words.x;
    uint death = words.y;
    if (packed_tag(packed) != cluster.tag)
    {
        arguments.InterlockedAdd(counters_offset + counter_ghosts_word * 4, 1);
        return false;
    }
    uint end = min(death, cluster.fade_end);
    uint tick_before = parameters.tick;
    if (tick_before >= end) return false;
    float4 position = asfloat(particles.Load4(address + particle_position_offset));
    float4 velocity = asfloat(particles.Load4(address + particle_velocity_offset));
    float size = asfloat(particles.Load(address + particle_size_offset));
    uint effect_class = packed_class(packed);
    uint layer_index = packed_layer(packed);
    Layer layer = load_layer(classes, effect_class, layer_index);
    RealismLayer lingering = load_realism_layer(classes, effect_class, layer_index);
    uint mask = frame.light_state.y;
    bool ring = layer.spawn == spawn_ring;
    Flow none = (Flow)0;
    uint steps = min(parameters.steps, maximum_simulation_steps);
    for (uint step = 0; step < steps && tick_before + step + 1 < end; ++step)
    {
        position.w += 1.0 / 60.0;
        lingering_step(position, velocity, size, layer, lingering, ring, 0, none, float3(0, 0, 0), 0, 1, mask);
    }
    if (steps != 0)
    {
        particles.Store4(address + particle_position_offset, asuint(position));
        particles.Store3(address + particle_velocity_offset, asuint(velocity.xyz));
    }
    return tick_before + steps < end && layer_enabled(mask, layer_index);
}

bool simulate_slab(uint slot, uint local)
{
    uint record = slot * explosion_particles + local;
    if (slot >= maximum_effects || record >= parameters.record_count) return false;
    uint effect_class = frame.effect_states[slot].w;
    uint layer_index = group_layer(classes, effect_class, local / simulation_group_size);
    EffectResidual link = load_effect_residual(slot);
    if (link.linger && layer_index <= layer_plume)
    {
        Layer layer = load_layer(classes, effect_class, layer_index);
        RealismLayer lingering = load_realism_layer(classes, effect_class, layer_index);
        if (lingering.survivor_stride != 0)
        {
            if (local - layer.first_group * simulation_group_size >= layer.count) return false;
            return simulate_lingering(slot, local, record, layer_index, layer, lingering, link);
        }
    }
    return simulate(slot, local, record);
}

[numthreads(simulation_group_size, 1, 1)]
void cs_realism(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    if (group.x >= min(frame.unit_state.y, maximum_unit_groups)) return;
    Unit unit = find_unit(group.x);
    if (thread.x < 2) alive_bits[thread.x] = 0;
    GroupMemoryBarrierWithGroupSync();
    uint group_in_unit = group.x - unit.first_group;
    bool alive = unit_is_cluster(unit) ? simulate_residual(unit.id - unit_cluster_first, group_in_unit, thread.x) :
        simulate_slab(unit.id, group_in_unit * simulation_group_size + thread.x);
    if (alive) InterlockedOr(alive_bits[thread.x / 32], 1u << (thread.x % 32));
    GroupMemoryBarrierWithGroupSync();
    if (thread.x == 0) alive_masks.Store2(group.x * 8, uint2(alive_bits[0], alive_bits[1]));
}
