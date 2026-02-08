RWTexture2DArray<float4> textureOut;

float4 DrawNBG(uint index)
{
    return float4(0.0f, index * 0.3333333333f, 1.0f, 1.0f);
}

float4 DrawRBG(uint index)
{
    return float4(index * 1.0f, 0.0f, 1.0f, 1.0f);
}

[numthreads(32, 16, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.z < 4)
    {
        textureOut[id] = DrawNBG(id.z);
    }
    else
    {
        textureOut[id] = DrawRBG(id.z - 4);
    }
}
