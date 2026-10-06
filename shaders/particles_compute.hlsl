struct Parameters
{
    uint capacity;
    uint steps;
    uint emitting;
    uint reset_epoch;
};
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<Parameters> parameters : register(b0);
RWByteAddressBuffer particles : register(u0);

uint hash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352d;
    value ^= value >> 15;
    value *= 0x846ca68b;
    return value ^ (value >> 16);
}

float random(uint id, uint salt)
{
    return float(hash(id ^ salt ^ 0x12345678) & 0xffff) / 65535.0;
}

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
        life = float3(-random(id, 1) * 2.0 - 0.016667, 0, 0);
        epoch = parameters.reset_epoch;
    }
    const float dt = 1.0 / 60.0;
    for (uint step = 0; step < min(parameters.steps, 4); ++step)
    {
        if (life.y > 0)
        {
            life.x += dt;
            motion.w -= 0.35 * dt;
            motion.xy += motion.zw * dt;
            if (life.x >= life.y) life.y = 0;
        }
        else if (parameters.emitting != 0)
        {
            life.x += dt;
            if (life.x >= 0)
            {
                motion = float4(0, -0.75, (random(id, 2) - 0.5) * 0.65, 0.7 + random(id, 3) * 0.45);
                life = float3(0, 2.0 + random(id, 4) * 2.0, 0.006 + random(id, 5) * 0.008);
            }
        }
        if (life.y == 0 && life.x > 0) life.x = 0;
    }
    particles.Store4(address, asuint(motion));
    particles.Store3(address + 16, asuint(life));
    particles.Store(address + 28, epoch);
}
