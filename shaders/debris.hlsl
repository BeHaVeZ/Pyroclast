#include "effect_classes.hlsli"
#include "explosion.hlsli"
#include "frame_constants.hlsli"
#include "lights.hlsli"

struct DrawConstants
{
    uint record_count;
    uint list_first;
    uint list_count;
};
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<DrawConstants> draw : register(b0);
ByteAddressBuffer particles : register(t0);
ByteAddressBuffer alive : register(t1);
ByteAddressBuffer classes : register(t2);

struct PixelInput
{
    float4 position : SV_Position;
    float3 world : TEXCOORD0;
    float3 normal : NORMAL;
    float3 albedo : COLOR0;
};

float3 rotate(float3 vector, float3 axis, float angle)
{
    float cosine = cos(angle);
    return vector * cosine + cross(axis, vector) * sin(angle) + axis * (dot(axis, vector) * (1 - cosine));
}

PixelInput vs_main(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    PixelInput output = (PixelInput)0;
    uint record = instance < draw.list_count ? alive.Load((draw.list_first + instance) * 4) : draw.record_count;
    if (record >= draw.record_count)
    {
        output.position = float4(0, 0, 0, 1);
        return output;
    }
    uint slot = record / explosion_particles;
    uint effect_class = frame.effect_states[slot].w;
    Layer layer = load_layer(classes, effect_class, layer_debris);
    uint address = record * particle_stride;
    float4 position = asfloat(particles.Load4(address + particle_position_offset));
    float lifetime = asfloat(particles.Load(address + particle_velocity_offset + 12));
    float size = asfloat(particles.Load(address + particle_size_offset));
    float spin = asfloat(particles.Load(address + particle_spin_offset));
    uint face = vertex / 6;
    uint corner = vertex % 6;
    float u = ((0x16u >> corner) & 1) != 0 ? 1 : -1;
    float w = ((0x34u >> corner) & 1) != 0 ? 1 : -1;
    uint axis = face / 2;
    float side = (face & 1) != 0 ? 1 : -1;
    float3 local = axis == 0 ? float3(side, u, w) : axis == 1 ? float3(u, side, w) : float3(u, w, side);
    float3 normal = axis == 0 ? float3(side, 0, 0) : axis == 1 ? float3(0, side, 0) : float3(0, 0, side);
    uint id = frame.effect_states[slot].x * explosion_particles + record % explosion_particles;
    float flat = 0.5 + 0.5 * particle_random(id, 11);
    float thin = 0.35 + 0.5 * particle_random(id, 12);
    float3 half_extent = 0.5 * size * float3(1, flat, thin) * saturate((lifetime - position.w) * 2);
    float distance = max(mul(frame.view_projection, float4(position.xyz, 1)).w, frame.camera_position.w);
    float pixels = 2 * half_extent.x * frame.projection.x / distance;
    if (pixels > 0) half_extent *= max(1, 1.5 / pixels);
    float axis_y = particle_random(id, 14) * 2 - 1;
    float axis_angle = particle_random(id, 15) * 6.2831853;
    float3 tumble = float3(sqrt(1 - axis_y * axis_y) * cos(axis_angle), axis_y, sqrt(1 - axis_y * axis_y) * sin(axis_angle));
    float reach = abs(rotate(float3(half_extent.x, 0, 0), tumble, spin).y) + abs(rotate(float3(0, half_extent.y, 0), tumble, spin).y) +
        abs(rotate(float3(0, 0, half_extent.z), tumble, spin).y);
    float3 centre = position.xyz - float3(0, 0.5 * size - reach, 0);
    output.world = centre + rotate(local * half_extent, tumble, spin);
    output.normal = rotate(normal, tumble, spin);
    output.position = mul(frame.view_projection, float4(output.world, 1));
    output.albedo = layer.first_color * (0.75 + 0.5 * particle_random(id, 13));
    return output;
}

float4 ps_main(PixelInput input) : SV_Target
{
    float3 normal = normalize(input.normal);
    float sun = saturate(dot(normal, frame.sun_direction.xyz));
    float3 ambient = lerp(frame.ground_color.rgb, frame.sky_color.rgb, normal.y * 0.5 + 0.5);
    float3 color = input.albedo * (frame.sun_color.rgb * sun + ambient + point_lights(input.world, normal, true));
    float visibility = exp(-frame.fog.a * length(input.world - frame.camera_position.xyz));
    return float4(lerp(frame.fog.rgb, color, visibility), 1);
}
