#define PYROCLAST_REALISM
#include "particles_lod.hlsl"
#include "particle_realism_vertex.hlsli"

RealismPixelInput vs_realism_lod(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    uint record = drawn_record(instance);
    if (record < min(draw.record_count, pool_records))
    {
        RealismFootprint realism = realism_footprint(particles, classes, record);
        if (realism.realism)
        {
            RealismPixelInput output = realism_vertex(vertex, realism);
            float opacity = output.base.color.a;
            if (draw.lod_pixels > 0)
            {
                float weight = saturate((2 * draw.lod_pixels - realism.footprint.pixels) / draw.lod_pixels);
                float even = 1 - pow(saturate(1 - opacity), 2 - weight);
                if (realism.survivor)
                {
                    opacity = (realism.survivor_index & 1) != 0 ? lerp(even, opacity * weight, realism.crossfade) : even;
                }
                else opacity = ((record % explosion_particles) & 1) != 0 ? opacity * weight : even;
            }
            if (draw.faint_floor > 0) opacity *= saturate((opacity - draw.faint_floor) / draw.faint_floor);
            if (opacity <= 0)
            {
                output.base.position = float4(0, 0, 0, 1);
                return output;
            }
            output.base.color.a = opacity;
            return output;
        }
    }
    return plain_pixel_input(realism_light(vs_lod(vertex, instance), record));
}
