#include "explosion.hlsli"

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
RWByteAddressBuffer particles : register(u0);

[numthreads(64, 1, 1)]
void cs_main(uint3 thread : SV_DispatchThreadID)
{
    uint id = thread.x;
    if (id >= parameters.capacity) return;
    uint address = id * 32;
    float4 motion = asfloat(particles.Load4(address));
    float3 life = asfloat(particles.Load3(address + 16));
    uint epoch = particles.Load(address + 28);
    if (epoch != parameters.reset_epoch)
    {
        motion = 0;
        life = 0;
        epoch = parameters.reset_epoch;
        if (id < min(parameters.burst_count, parameters.capacity))
        {
            float angle = particle_random(id, 1) * 6.2831853;
            float2 direction = float2(cos(angle), sin(angle));
            if (id >= smoke_first)
            {
                motion = float4(float2(0, -0.15) + direction * particle_random(id, 3) * 0.07,
                    direction * (0.10 + particle_random(id, 2) * 0.15) + float2(0, 0.12));
                life = float3(0, 6.0 + particle_random(id, 4) * 2.0, 0.04 + particle_random(id, 5) * 0.04);
            }
            else
            {
                bool fireball = id < fireball_particles;
                float speed = fireball ? 0.08 + particle_random(id, 2) * 0.25 : 0.35 + particle_random(id, 2) * 0.85;
                motion = float4(float2(0, -0.15) + direction * particle_random(id, 3) * 0.025,
                    direction * speed + float2(0, fireball ? 0.05 : 0.25));
                life = float3(0,
                    fireball ? 0.35 + particle_random(id, 4) * 0.45 : 1.0 + particle_random(id, 4) * 1.2,
                    fireball ? 0.04 + particle_random(id, 5) * 0.025 : 0.003 + particle_random(id, 5) * 0.004);
            }
        }
    }
    const float dt = 1.0 / 60.0;
    for (uint step = 0; step < min(parameters.steps, 4); ++step)
    {
        if (life.y > 0)
        {
            life.x += dt;
            if (id >= smoke_first)
            {
                float temperature = smoke_temperature(life.x);
                motion.zw += (float2(0.08, 0) - motion.zw) * (1.4 * dt);
                motion.w += 0.20 * temperature * dt;
            }
            else if (id >= fireball_particles) motion.w -= 0.65 * dt;
            motion.xy += motion.zw * dt;
            if (life.x >= life.y) life.y = 0;
        }
    }
    particles.Store4(address, asuint(motion));
    particles.Store3(address + 16, asuint(life));
    particles.Store(address + 28, epoch);
}
