#define PYROCLAST_REALISM
#include "particles_lod.hlsl"
#include "particle_realism_vertex.hlsli"
#ifdef PYROCLAST_ROTOR
#include "rotor_draw.hlsli"
#endif

RealismPixelInput vs_realism_lod(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    uint record = drawn_record(instance);
#ifdef PYROCLAST_ROTOR
    if (record < min(draw.record_count, residual_first) && rotor_record(record))
    {
        RealismPixelInput rotor = rotor_vertex(vertex, record);
        if (load_effect_residual(record / explosion_particles).rotor_disc) return rotor;
        float rotor_opacity = rotor.base.color.a;
        if (draw.lod_pixels > 0)
        {
            float pixels = rotor_footprint(particles, classes, record).footprint.pixels;
            float weight = saturate((2 * draw.lod_pixels - pixels) / draw.lod_pixels);
            float even = 1 - pow(saturate(1 - rotor_opacity), 2 - weight);
            rotor_opacity = ((record % explosion_particles) & 1) != 0 ? rotor_opacity * weight : even;
        }
        if (draw.faint_floor > 0) rotor_opacity *= saturate((rotor_opacity - draw.faint_floor) / draw.faint_floor);
        if (rotor_opacity <= 0) rotor.base.position = float4(0, 0, 0, 1);
        rotor.base.color.a = rotor_opacity;
        return rotor;
    }
#endif
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
