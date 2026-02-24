struct Config {
    uint numPolys;
    uint3 _reserved;
};

struct PolyParams {
    uint atlasPos;
    uint size;
    uint fbPos;
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

ByteAddressBuffer polyIn : register(t0);
StructuredBuffer<PolyParams> polyParams : register(t1);

RWByteAddressBuffer fbOut : register(u0);

// -----------------------------------------------------------------------------

static const uint kAtlasStride = 1024;

// -----------------------------------------------------------------------------

bool BitTest(uint value, uint bit) {
    return ((value >> bit) & 1) != 0;
}

uint BitExtract(uint value, uint offset, uint length) {
    const uint mask = (1u << length) - 1u;
    return (value >> offset) & mask;
}

int SignExtend(int value, int bits) {
    const uint shift = 32 - bits;
    return (value << shift) >> shift;
}

uint2 Extract16PairU(uint value32) {
    return uint2(
        BitExtract(value32, 0, 16),
        BitExtract(value32, 16, 16)
    );
}

int2 Extract16PairSX(uint value32, int bits) {
    return uint2(
        SignExtend(value32, bits),
        SignExtend((value32 >> 16), bits)
    );
}

int2 Extract16PairS(uint value32) {
    return Extract16PairSX(value32, 16);
}

uint ByteSwap16(uint val) {
    return ((val >> 8) & 0x00FF) |
           ((val << 8) & 0xFF00);
}

uint ByteSwap32(uint val) {
    return ((val >> 24) & 0x000000FF) |
           ((val >> 8) & 0x0000FF00) |
           ((val << 8) & 0x00FF0000) |
           ((val << 24) & 0xFF000000);
}

void WriteFBOut8(uint address, uint data) {
    const uint shift = (address & 3) * 8;
    const uint mask = 0xFF << shift;
    data = (data & 0xFF) << shift;
             
    address &= ~3;
    uint dummy;
    fbOut.InterlockedAnd(address, mask, dummy);
    fbOut.InterlockedOr(address, data, dummy);
}

void WriteFBOut16(uint address, uint data) {
    const uint shift = (address & 2) * 8;
    const uint mask = 0xFFFF << shift;
    data = (data & 0xFFFF) << shift;
             
    address &= ~3;
    uint dummy;
    fbOut.InterlockedAnd(address, mask, dummy);
    fbOut.InterlockedOr(address, data, dummy);
}

// -----------------------------------------------------------------------------

void MergePolys(uint2 pos) {
    for (uint i = 0; i < config.numPolys; i++) {
        const PolyParams poly = polyParams[i];

        const int2 fbPos = Extract16PairS(poly.fbPos);
        const int2 size = Extract16PairS(poly.size);

        // Skip out of bounds pixels
        if (any(pos < fbPos) || any(pos >= fbPos + size)) {
            continue;
        }
        
        const int2 relPos = pos - fbPos;
        const int2 atlasPos = Extract16PairS(poly.atlasPos) + relPos;
        
        const uint atlasAddr = (atlasPos.x + atlasPos.y * kAtlasStride) * 4;
        const uint rawValue = polyIn.Load(atlasAddr);
                
        // TODO: 8-bit/16-bit mode
        // TODO: framebuffer dimensions
        const uint fbAddr = (pos.x + pos.y * 512) * 2;
        WriteFBOut16(fbAddr, rawValue);
    }
}

// -----------------------------------------------------------------------------

[numthreads(32, 32, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    MergePolys(id.xy);
}
