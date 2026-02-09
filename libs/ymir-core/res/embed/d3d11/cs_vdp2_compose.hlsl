Texture2DArray<uint4> textureBGs : register(t0);
RWTexture2D<float4> textureOut : register(u0);

[numthreads(32, 16, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    // TODO: compose image
    // The alpha channel of the BG textures contains pixel attributes:
    // bits  use
    //  0-3  Priority (0 to 7)
    //    6  Special color calculation flag
    //    7  Transparent flag (0=opaque, 1=transparent)
    textureOut[id.xy] = float4(textureBGs[uint3(id.xy, 0)].xyz / 255.0, 1.0f);
}
