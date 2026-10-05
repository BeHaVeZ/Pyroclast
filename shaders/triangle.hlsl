struct VertexInput
{
    float3 position : POSITION;
    float3 color : COLOR;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 color : COLOR;
};

VertexOutput vs_main(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position, 1.0f);
    output.color = input.color;
    return output;
}

float4 ps_main(VertexOutput input) : SV_Target
{
    return float4(input.color, 1.0f);
}
