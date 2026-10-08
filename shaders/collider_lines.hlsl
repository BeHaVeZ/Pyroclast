#include "frame_constants.hlsli"
#include "terrain_layout.hlsli"

struct DrawConstants { uint box_count; };
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<DrawConstants> draw : register(b0);
ByteAddressBuffer environment : register(t0);

struct PixelInput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

PixelInput vs_main(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    PixelInput output = (PixelInput)0;
    if (instance >= min(draw.box_count, maximum_prop_boxes) || vertex >= 24)
    {
        output.position = float4(0, 0, 0, 1);
        return output;
    }
    float4 first = asfloat(environment.Load4(environment_boxes_offset + instance * prop_box_stride));
    float4 second = asfloat(environment.Load4(environment_boxes_offset + instance * prop_box_stride + 16));
    uint edge = vertex / 2;
    uint axis = edge / 4;
    uint others = edge % 4;
    uint corner = (others & ((1u << axis) - 1)) | ((others >> axis) << (axis + 1)) | (vertex % 2 == 1 ? 1u << axis : 0);
    float3 local = float3(corner & 1 ? 1 : -1, corner & 2 ? 1 : -1, corner & 4 ? 1 : -1) * second.xyz;
    float3 world = first.xyz + float3(local.x * first.w - local.z * second.w, local.y, local.x * second.w + local.z * first.w);
    uint2 open = environment.Load2(environment_flags_offset);
    bool open_bottom = (((instance < 32 ? open.x : open.y) >> (instance % 32)) & 1) != 0;
    output.color = open_bottom ? float3(1.6, 0.7, 0.2) : float3(0.3, 1.5, 0.5);
    output.position = mul(frame.view_projection, float4(world, 1));
    return output;
}
