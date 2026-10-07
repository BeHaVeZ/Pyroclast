#include "explosion.hlsli"
#include "frame_constants.hlsli"
#include "terrain.hlsli"

struct Parameters
{
    uint capacity;
    uint steps;
    uint burst_count;
    uint reset_epoch;
};
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<Parameters> parameters : register(b0);
ByteAddressBuffer heights : register(t0);
RWByteAddressBuffer particles : register(u0);

static const float gravity = 9.81;
static const float3 wind = float3(2.0, 0, 0);

[numthreads(simulation_group_size, 1, 1)]
void cs_main(uint3 thread : SV_DispatchThreadID)
{
    uint id = thread.x;
    if (id >= parameters.capacity) return;
    uint count = parameters.burst_count / explosion_particles;
    uint emissive_count = count * emissive_particles;
    bool smoke = id >= emissive_count;
    uint effect = smoke ? (id - emissive_count) / smoke_particles : id / emissive_particles;
    uint local = smoke ? emissive_particles + (id - emissive_count) % smoke_particles : id % emissive_particles;
    bool fireball = !smoke && local < fireball_particles;
    uint address = id * particle_stride;
    float4 position = asfloat(particles.Load4(address + particle_position_offset));
    float4 velocity = asfloat(particles.Load4(address + particle_velocity_offset));
    float size = asfloat(particles.Load(address + particle_size_offset));
    uint epoch = particles.Load(address + particle_epoch_offset);
    if (epoch != parameters.reset_epoch)
    {
        position = 0;
        velocity = 0;
        size = 0;
        epoch = parameters.reset_epoch;
        if (id < min(parameters.burst_count, parameters.capacity))
        {
            float3 origin = frame.effect_origins[min(effect, maximum_explosions - 1)].xyz;
            float3 direction = particle_direction(id);
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
        }
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
                velocity.y = fireball || smoke ? max(velocity.y, 0) : -velocity.y * 0.35;
                if (!fireball && !smoke) velocity.xz *= 0.7;
            }
            if (position.w >= velocity.w) velocity.w = 0;
        }
    }
    particles.Store4(address + particle_position_offset, asuint(position));
    particles.Store4(address + particle_velocity_offset, asuint(velocity));
    particles.Store2(address + particle_size_offset, uint2(asuint(size), epoch));
}
