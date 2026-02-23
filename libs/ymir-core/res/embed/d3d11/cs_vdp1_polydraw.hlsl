struct Config {
    uint4 _reserved;
};

struct PolyParams {
    uint atlasPos;
    uint sysClip;
    uint userClipX;
    uint userClipY;
    uint localCoord;
    uint cmdAddress;
};

// -----------------------------------------------------------------------------

cbuffer Config : register(b0) {
    Config config;
}

ByteAddressBuffer vram : register(t0);
StructuredBuffer<PolyParams> polyParams : register(t1);

RWByteAddressBuffer polyOut : register(u0);

// -----------------------------------------------------------------------------

bool BitTest(uint value, uint bit) {
    return ((value >> bit) & 1) != 0;
}

uint BitExtract(uint value, uint offset, uint length) {
    const uint mask = (1u << length) - 1u;
    return (value >> offset) & mask;
}

// -----------------------------------------------------------------------------

[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    // TODO: draw sprites
    // - id.z selects a sprite to draw
    // - id.xy are not used
    // - sprites are drawn sequentially due to the forward rendering method used by the Saturn
    // - each thread draws one full sprite into a dedicated framebuffer texture slice
    //   - must clear target area before rendering sprite
    // TODO: figure out if it is possible to efficiently parallelize individual sprite rendering
    
    const PolyParams poly = polyParams[id.z];
    // const uint atlasPosX = BitExtract(poly.atlasPos, 0, 16);
    // const uint atlasPosY = BitExtract(poly.atlasPos, 16, 16);
    // const uint sysClipH = BitExtract(poly.sysClip, 0, 16);
    // const uint sysClipV = BitExtract(poly.sysClip, 16, 16);
    // const uint userClipX0 = BitExtract(poly.userClipX, 0, 16);
    // const uint userClipX1 = BitExtract(poly.userClipX, 16, 16);
    // const uint userClipY0 = BitExtract(poly.userClipY, 0, 16);
    // const uint userClipY1 = BitExtract(poly.userClipY, 16, 16);
    // const uint localCoordX = BitExtract(poly.localCoord, 0, 16);
    // const uint localCoordY = BitExtract(poly.localCoord, 16, 16);

    polyOut.Store(id.z * 16, poly.atlasPos);
    polyOut.Store(id.z * 16 + 4, poly.cmdAddress);
    polyOut.Store(id.z * 16 + 8, poly.localCoord);
    polyOut.Store(id.z * 16 + 12, poly.sysClip);
}
