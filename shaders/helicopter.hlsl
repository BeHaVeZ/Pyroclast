#include "props.hlsl"

struct HelicopterConstants
{
    float4 rows[3];
    uint4 state;
};
#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<HelicopterConstants> helicopter : register(b0);

PixelInput vs_helicopter(uint vertex : SV_VertexID)
{
    uint address = vertex * prop_vertex_stride;
    uint4 first = vertices.Load4(address);
    float4 second = asfloat(vertices.Load4(address + 16));
    float4 body = float4(asfloat(first.xyz), 1);
    PixelInput output;
    output.world = float3(dot(helicopter.rows[0], body), dot(helicopter.rows[1], body), dot(helicopter.rows[2], body));
    output.material = first.w;
    output.normal = float3(dot(helicopter.rows[0].xyz, second.xyz), dot(helicopter.rows[1].xyz, second.xyz), dot(helicopter.rows[2].xyz, second.xyz));
    output.shade = second.w;
    output.position = mul(frame.view_projection, float4(output.world, 1));
    return output;
}

float4 ps_helicopter(PixelInput input) : SV_Target
{
    float4 color = ps_main(input);
    uint2 cell = (uint2(input.position.xy) >> 1) & 3u;
    uint mixed = cell.x ^ cell.y;
    uint bayer = ((mixed & 1u) << 3) | ((cell.y & 1u) << 2) | (mixed & 2u) | ((cell.y >> 1) & 1u);
    if (bayer >= helicopter.state.x) discard;
    return color;
}
