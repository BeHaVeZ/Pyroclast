static const uint fireball_particles = 256;

uint particle_hash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352d;
    value ^= value >> 15;
    value *= 0x846ca68b;
    return value ^ (value >> 16);
}

float particle_random(uint id, uint salt)
{
    return float(particle_hash(id ^ salt ^ 0x12345678) & 0xffff) / 65535.0;
}
