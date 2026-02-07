struct PSInput
{
    float4 pos : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target0
{
    return float4(input.texCoord, 0.0f, 1.0f);
}
