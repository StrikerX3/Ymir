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
    
    // HACK: very simple compositing
    uint3 outColor = uint3(0, 0, 0);
    int maxPriority = -1;
    for (uint i = 0; i < 4; i++) {
        const uint4 pixel = textureBGs[uint3(id.xy, i)];
        const bool transparent = (pixel.a >> 7) & 1;
        if (transparent) {
            continue;
        }
        
        const int priority = pixel.a & 7;
        if (priority > maxPriority) {
            maxPriority = priority;
            outColor = pixel.rgb;
        }
    }
    
    textureOut[id.xy] = float4(outColor / 255.0, 1.0f);
}
