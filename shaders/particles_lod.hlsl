// The thinning radius and faint floor are draw constant words 4-5, after the four vs_main reads.
#define PARTICLE_LOD_CONSTANTS
#include "particles.hlsl"
#include "particle_footprint.hlsli"

PixelInput vs_lod(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    PixelInput output = vs_main(vertex, instance);
    uint record = instance < draw.list_count ? alive.Load((draw.list_first + instance) * 4) : draw.record_count;
    if (record >= draw.record_count) return output;
    uint slot = record / explosion_particles;
    uint local = record % explosion_particles;
    uint effect_class = frame.effect_states[slot].w;
    uint layer_index = group_layer(classes, effect_class, local / simulation_group_size);
    if (!smoke_like(layer_index)) return output;
    Footprint footprint = particle_footprint(particles, record, load_layer(classes, effect_class, layer_index), true);
    float opacity = output.color.a;
    if (draw.lod_pixels > 0)
    {
        float weight = saturate((2 * draw.lod_pixels - footprint.pixels) / draw.lod_pixels);
        opacity = (local & 1) != 0 ? opacity * weight : 1 - pow(saturate(1 - opacity), 2 - weight);
    }
    if (opacity <= 0 || (draw.faint_floor > 0 && opacity < draw.faint_floor))
    {
        output.position = float4(0, 0, 0, 1);
        return output;
    }
    output.color.a = opacity;
    return output;
}
