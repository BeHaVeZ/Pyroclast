#include "effect_classes.hlsli"
#include "explosion.hlsli"
#include "frame_constants.hlsli"
#include "terrain.hlsli"

struct Parameters
{
    uint steps;
    uint effect_count;
    uint record_count;
    uint reserved;
};
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<Parameters> parameters : register(b0);
ByteAddressBuffer heights : register(t0);
ByteAddressBuffer classes : register(t1);
RWByteAddressBuffer particles : register(u0);
RWByteAddressBuffer alive_masks : register(u1);

static const float3 wind = float3(2.0, 0, 0);

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
    if (generation != state.y)
    {
        generation = state.y;
        float3 origin = frame.effect_origins[slot].xyz;
        float3 normal = frame.effect_normals[slot].xyz;
        uint id = state.x * explosion_particles + local;
        float3 direction = around_normal(particle_direction(id), normal);
        position.xyz = origin + direction * particle_random(id, 3) * layer.radius;
        velocity.xyz = direction * (layer.speed_min + particle_random(id, 2) * layer.speed_range) + float3(0, layer.up_kick, 0);
        velocity.w = layer.lifetime_min + particle_random(id, 4) * layer.lifetime_range;
        size = layer.size_min + particle_random(id, 5) * layer.size_range;
        position.w = 0;
        if (layer.delay_range > 0) position.w = -particle_random(id, 7) * layer.delay_range;
    }
    bool smoke = smoke_like(layer_index);
    const float dt = 1.0 / 60.0;
    for (uint step = 0; step < min(parameters.steps, maximum_simulation_steps); ++step)
    {
        if (velocity.w > 0)
        {
            position.w += dt;
            if (position.w < 0) continue;
            if (layer.drag > 0) velocity.xyz += (wind - velocity.xyz) * (layer.drag * dt);
            if (layer.buoyancy > 0) velocity.y += layer.buoyancy * smoke_temperature(position.w) * dt;
            if (layer.gravity > 0) velocity.y -= layer.gravity * dt;
            position.xyz += velocity.xyz * dt;
            float ground = terrain_height(heights, position.xz) + (smoke ? size : 0);
            if (position.y < ground)
            {
                position.y = ground;
                bool bounce = layer.restitution > 0 && velocity.y < 0;
                if (bounce) velocity.xz *= layer.friction;
                velocity.y = bounce ? -velocity.y * layer.restitution : max(velocity.y, 0);
            }
            if (position.w >= velocity.w) velocity.w = 0;
        }
    }
    particles.Store4(address + particle_position_offset, asuint(position));
    particles.Store4(address + particle_velocity_offset, asuint(velocity));
    particles.Store2(address + particle_size_offset, uint2(asuint(size), generation));
    return velocity.w > 0 && position.w >= 0;
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
