#include "particle_layout.hlsli"

float smoke_temperature(float age) { return exp(-0.45 * age); }

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

float3 particle_direction(uint id)
{
    float y = particle_random(id, 1);
    float angle = particle_random(id, 6) * 6.2831853;
    float radius = sqrt(1 - y * y);
    return float3(radius * cos(angle), y, radius * sin(angle));
}
