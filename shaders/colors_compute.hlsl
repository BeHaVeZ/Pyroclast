struct ComputeParameters
{
    uint vertex_count;
    uint color_phase;
};

#ifdef __spirv__
[[vk::push_constant]]
#endif
ConstantBuffer<ComputeParameters> parameters : register(b0);
RWByteAddressBuffer vertices : register(u0);

[numthreads(4, 1, 1)]
void cs_main(uint3 thread : SV_DispatchThreadID)
{
    uint index = thread.x;
    if (index >= parameters.vertex_count)
    {
        return;
    }
    uint channel = index % 3;
    float3 color = float3(channel == 0, channel == 1, channel == 2);
    if (parameters.color_phase != 0)
    {
        color = 1.0f - color;
    }
    vertices.Store3(index * 24 + 12, asuint(color));
}
