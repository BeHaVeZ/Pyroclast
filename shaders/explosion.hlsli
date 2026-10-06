#include "particle_layout.hlsli"

float2 explosion_origin(uint effect, uint count)
{
    if (count == 1) return float2(0, -0.15);
    uint columns = count == 10 ? 5 : 10;
    uint rows = (count + columns - 1) / columns;
    return float2(-0.65 + 1.3 * (effect % columns) / (columns - 1),
        -0.35 + 0.4 * (effect / columns) / max(rows - 1, 1u));
}

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
