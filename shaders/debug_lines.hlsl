#include "frame_constants.hlsli"

struct DrawConstants { uint effect_count; };
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<DrawConstants> draw : register(b0);

static const uint vertices_per_effect = 32;

struct PixelInput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

float3 box_corner(uint corner)
{
    return float3(corner & 1 ? 36 : -20, corner & 2 ? 40 : -2, corner & 4 ? 20 : -20);
}

PixelInput vs_main(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    PixelInput output = (PixelInput)0;
    uint rank = min(instance, maximum_effects - 1);
    uint slot = frame.effect_order[rank / 4][rank % 4];
    if (instance >= draw.effect_count || slot >= maximum_effects || vertex >= vertices_per_effect)
    {
        output.position = float4(0, 0, 0, 1);
        return output;
    }
    float3 origin = frame.effect_origins[slot].xyz;
    float3 position = origin;
    if (vertex < 6)
    {
        float3 axis = float3(vertex / 2 == 0, vertex / 2 == 1, vertex / 2 == 2);
        position += axis * (vertex % 2 == 0 ? -1 : 1);
        output.color = float3(1.6, 1.3, 0.2);
    }
    else if (vertex < 8)
    {
        position += frame.effect_normals[slot].xyz * (vertex == 7 ? 4 : 0);
        output.color = float3(0.2, 1.2, 1.6);
    }
    else
    {
        uint edge = (vertex - 8) / 2;
        uint axis = edge / 4;
        uint others = edge % 4;
        uint corner = (others & ((1u << axis) - 1)) | ((others >> axis) << (axis + 1));
        position += box_corner(corner | (vertex % 2 == 1 ? 1u << axis : 0));
        output.color = float3(0.7, 0.7, 0.7);
    }
    output.position = mul(frame.view_projection, float4(position, 1));
    return output;
}

float4 ps_main(PixelInput input) : SV_Target
{
    return float4(input.color, 1);
}
