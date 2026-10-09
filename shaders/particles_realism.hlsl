#define PYROCLAST_REALISM
#include "particles.hlsl"
#include "particle_realism_vertex.hlsli"

RealismPixelInput vs_realism(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    uint record = drawn_record(instance);
    if (record < min(draw.record_count, pool_records))
    {
        RealismFootprint realism = realism_footprint(particles, classes, record);
        if (realism.realism) return realism_vertex(vertex, realism);
    }
    return plain_pixel_input(realism_light(vs_main(vertex, instance), record));
}
