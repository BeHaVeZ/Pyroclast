#include "effect_classes.hlsli"
#include "environment.hlsli"
#include "explosion.hlsli"
#include "frame_constants.hlsli"
#include "noise.hlsli"

struct Parameters
{
    uint steps;
    uint effect_count;
    uint record_count;
    uint tick;
};
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<Parameters> parameters : register(b0);
ByteAddressBuffer environment : register(t0);
ByteAddressBuffer classes : register(t1);
RWByteAddressBuffer particles : register(u0);
RWByteAddressBuffer alive_masks : register(u1);

float3 wind_at(float height)
{
    return frame.wind.xyz * (0.35 + 0.65 * saturate(height / 15));
}

float3 vortex_flow(Flow flow, float3 origin, float3 position, float seconds)
{
    float2 ring = ring_at(flow, seconds);
    float3 offset = position - origin;
    float radial_distance = length(offset.xz);
    float2 outward = radial_distance > 1e-4 ? offset.xz / radial_distance : float2(0, 0);
    float2 from_core = float2(radial_distance - ring.y, offset.y - ring.x);
    float decay = exp(-max(seconds - flow.rise_time, 0) / max(flow.decay_time, 1e-3));
    float2 turn = flow.strength * decay * float2(from_core.y, -from_core.x) /
        (dot(from_core, from_core) + flow.core_radius * flow.core_radius);
    turn.x *= saturate(radial_distance / max(flow.core_radius, 1e-3));
    return float3(outward.x * turn.x, turn.y, outward.y * turn.x);
}

groupshared uint alive_bits[2];

float3 around_normal(float3 direction, float3 normal)
{
    float3 tangent = normalize(cross(abs(normal.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0), normal));
    float3 bitangent = cross(normal, tangent);
    return tangent * direction.x + normal * direction.y + bitangent * direction.z;
}

bool simulate(uint slot, uint local, uint record)
{
    uint4 state = frame.effect_states[slot];
    uint layer_index = group_layer(classes, state.w, local / simulation_group_size);
    if (layer_index >= layer_count) return false;
    Layer layer = load_layer(classes, state.w, layer_index);
    if (local - layer.first_group * simulation_group_size >= layer.count) return false;
    uint address = record * particle_stride;
    float4 position = asfloat(particles.Load4(address + particle_position_offset));
    float4 velocity = asfloat(particles.Load4(address + particle_velocity_offset));
    float size = asfloat(particles.Load(address + particle_size_offset));
    uint generation = particles.Load(address + particle_generation_offset);
    float spin = asfloat(particles.Load(address + particle_spin_offset));
    uint mask = frame.light_state.y;
    bool props = mask_enabled(mask, props_mask_bit) && mask_enabled(mask, collision_mask_bit);
    bool deflect = props && layer.drag > 0 && mask_enabled(mask, deflection_mask_bit);
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
        velocity.w = layer.lifetime_min + particle_random(id, 4) * layer.lifetime_range;
        size = layer.size_min + particle_random(id, 5) * layer.size_range;
        position.w = 0;
        if (layer.delay_range > 0) position.w = -particle_random(id, 7) * layer.delay_range;
        spin = 0;
    }
    bool smoke = smoke_like(layer_index);
    bool debris = layer_index == layer_debris;
    float radius = smoke ? min(size, prop_contact_limit) : debris ? 0.5 * size : 0;
    uint2 open_bottom = props ? load_open_bottoms(environment) : uint2(0, 0);
    Flow flow = load_flow(classes, state.w);
    float3 effect_origin = frame.effect_origins[slot].xyz;
    bool vortex = smoke && flow.strength > 0 && mask_enabled(mask, vortex_mask_bit);
    bool curl = smoke && layer.turbulence > 0 && mask_enabled(mask, curl_mask_bit);
    float spin_rate = debris ? (particle_random(state.x * explosion_particles + local, 10) - 0.5) * 24 : 0;
    const float dt = 1.0 / 60.0;
    for (uint step = 0; step < min(parameters.steps, maximum_simulation_steps); ++step)
    {
        if (velocity.w > 0)
        {
            position.w += dt;
            if (position.w < 0) continue;
            float terrain = terrain_height(environment, position.xz);
            uint2 near = props ? nearby_boxes(environment, position.xyz, !ring) : uint2(0, 0);
            if (layer.drag > 0)
            {
                float seconds = float(parameters.tick + step + 1 - state.z) / 60.0;
                float3 air = wind_at(position.y - terrain);
                if (vortex) air += vortex_flow(flow, effect_origin, position.xyz, seconds);
                if (curl) air += curl_noise(position.xyz * 0.125 - float3(0, seconds * 0.2, 0)) * (layer.turbulence * exp(-0.3 * position.w));
                if (deflect && any(near != 0)) air = deflect_air(environment, near, position.xyz, radius, air);
                velocity.xyz += (air - velocity.xyz) * (layer.drag * dt);
            }
            if (layer.buoyancy > 0) velocity.y += layer.buoyancy * layer_temperature(layer, position.w) * dt;
            if (layer.gravity > 0) velocity.y -= layer.gravity * dt;
            float3 start = position.xyz;
            position.xyz += velocity.xyz * dt;
            terrain = terrain_height(environment, position.xz);
            float ground = terrain + (smoke ? size : debris ? size * 0.5 : 0);
            if (position.y < ground)
            {
                position.y = ground;
                bool bounce = layer.restitution > 0 && velocity.y < 0;
                if (bounce) velocity.xz *= layer.friction;
                velocity.y = bounce ? -velocity.y * layer.restitution : max(velocity.y, 0);
                if (debris && dot(velocity.xyz, velocity.xyz) < 0.5) velocity.xyz = 0;
            }
            float support = terrain;
            if (props)
            {
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
                        float3 normal = prop_contact(box, ((open >> bit) & 1) != 0, start, position.xyz, velocity.xyz, radius,
                            layer.restitution, layer.friction);
                        if (debris && normal.y > 0.7 && dot(velocity.xyz, velocity.xyz) < 0.5) velocity.xyz = 0;
                        if (ring) support = max(support, prop_support(box, position.xyz, radius));
                    }
                }
            }
            if (debris) spin += spin_rate * dt * saturate(length(velocity.xyz) * 0.25);
            if (smoke && !ring)
            {
                float clearance = position.y - terrain;
                if (clearance < 2 * size) velocity.y += (2 * size - clearance) * 1.5 * dt;
            }
            if (ring) position.y = min(position.y, support + size + 0.3 * position.w);
            if (position.w >= velocity.w) velocity.w = 0;
        }
    }
    particles.Store4(address + particle_position_offset, asuint(position));
    particles.Store4(address + particle_velocity_offset, asuint(velocity));
    particles.Store3(address + particle_size_offset, uint3(asuint(size), generation, asuint(spin)));
    return velocity.w > 0 && position.w >= 0 && layer_enabled(mask, layer_index);
}

[numthreads(simulation_group_size, 1, 1)]
void cs_main(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    uint rank = group.x / slab_groups;
    if (rank >= min(parameters.effect_count, maximum_effects)) return;
    uint slot = frame.effect_order[rank / 4][rank % 4];
    uint local = (group.x % slab_groups) * simulation_group_size + thread.x;
    uint record = slot * explosion_particles + local;
    if (thread.x < 2) alive_bits[thread.x] = 0;
    GroupMemoryBarrierWithGroupSync();
    if (slot < maximum_effects && record < parameters.record_count && simulate(slot, local, record))
    {
        InterlockedOr(alive_bits[thread.x / 32], 1u << (thread.x % 32));
    }
    GroupMemoryBarrierWithGroupSync();
    if (thread.x == 0) alive_masks.Store2(group.x * 8, uint2(alive_bits[0], alive_bits[1]));
}
