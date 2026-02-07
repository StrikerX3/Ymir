struct VSInput
{
    float3 pos : POSITION;
    float2 texCoord : TEXCOORD0;
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.pos = float4(input.pos, 1.0f);
    output.texCoord = input.texCoord;
    return output;
}
