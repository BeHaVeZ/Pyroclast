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
RWByteAddressBuffer particles : register(u0);

static const float gravity = 9.81;
static const float3 wind = float3(2.0, 0, 0);
static const uint groups_per_slab = explosion_particles / simulation_group_size;

float3 around_normal(float3 direction, float3 normal)
{
    float3 tangent = normalize(cross(abs(normal.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0), normal));
    float3 bitangent = cross(normal, tangent);
    return tangent * direction.x + normal * direction.y + bitangent * direction.z;
}

[numthreads(simulation_group_size, 1, 1)]
void cs_main(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    uint rank = group.x / groups_per_slab;
    if (rank >= min(parameters.effect_count, maximum_effects)) return;
    uint slot = frame.effect_order[rank / 4][rank % 4];
    uint local = (group.x % groups_per_slab) * simulation_group_size + thread.x;
    uint record = slot * explosion_particles + local;
    if (slot >= maximum_effects || record >= parameters.record_count) return;
    bool smoke = local < smoke_particles;
    bool fireball = !smoke && local - smoke_particles < fireball_particles;
    uint address = record * particle_stride;
    float4 position = asfloat(particles.Load4(address + particle_position_offset));
    float4 velocity = asfloat(particles.Load4(address + particle_velocity_offset));
    float size = asfloat(particles.Load(address + particle_size_offset));
    uint generation = particles.Load(address + particle_generation_offset);
    uint4 state = frame.effect_states[slot];
    if (generation != state.y)
    {
        generation = state.y;
        float3 origin = frame.effect_origins[slot].xyz;
        float3 normal = frame.effect_normals[slot].xyz;
        uint id = state.x * explosion_particles + local;
        float3 direction = around_normal(particle_direction(id), normal);
        if (smoke)
        {
            position.xyz = origin + direction * particle_random(id, 3) * 1.75;
            velocity.xyz = direction * (2.5 + particle_random(id, 2) * 3.75) + float3(0, 3.0, 0);
            velocity.w = 6.0 + particle_random(id, 4) * 2.0;
            size = 1.0 + particle_random(id, 5) * 1.0;
        }
        else
        {
            float speed = fireball ? 2.0 + particle_random(id, 2) * 6.25 : 6.0 + particle_random(id, 2) * 10.0;
            position.xyz = origin + direction * particle_random(id, 3) * 0.625;
            velocity.xyz = direction * speed + float3(0, fireball ? 1.25 : 4.0, 0);
            velocity.w = fireball ? 0.35 + particle_random(id, 4) * 0.45 : 1.0 + particle_random(id, 4) * 1.2;
            size = fireball ? 1.0 + particle_random(id, 5) * 0.625 : 0.06 + particle_random(id, 5) * 0.08;
        }
        position.w = 0;
    }
    const float dt = 1.0 / 60.0;
    for (uint step = 0; step < min(parameters.steps, maximum_simulation_steps); ++step)
    {
        if (velocity.w > 0)
        {
            position.w += dt;
            if (smoke)
            {
                float temperature = smoke_temperature(position.w);
                velocity.xyz += (wind - velocity.xyz) * (1.4 * dt);
                velocity.y += 5.0 * temperature * dt;
            }
            else if (!fireball) velocity.y -= gravity * dt;
            position.xyz += velocity.xyz * dt;
            float ground = terrain_height(heights, position.xz) + (smoke ? size : 0);
            if (position.y < ground)
            {
                position.y = ground;
                bool bounce = !fireball && !smoke && velocity.y < 0;
                if (bounce) velocity.xz *= 0.7;
                velocity.y = bounce ? -velocity.y * 0.35 : max(velocity.y, 0);
            }
            if (position.w >= velocity.w) velocity.w = 0;
        }
    }
    particles.Store4(address + particle_position_offset, asuint(position));
    particles.Store4(address + particle_velocity_offset, asuint(velocity));
    particles.Store2(address + particle_size_offset, uint2(asuint(size), generation));
}
