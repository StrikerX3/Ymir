struct Config {
    uint params;
    uint erase;
    uint eraseWriteValue;
    uint scaling;
};

// -----------------------------------------------------------------------------

cbuffer Config : register(b0) {
    Config config;
}

ByteAddressBuffer cpuWriteBitmap : register(t0);

RWByteAddressBuffer fbOut : register(u0);

// -----------------------------------------------------------------------------

static const uint kFBSize = 256 * 1024;
static const uint kDeinterlaceFBOffset = 2 * kFBSize;
static const uint kMeshFBOffset = 2 * 2 * kFBSize;

// -----------------------------------------------------------------------------

bool BitTest(uint value, uint bit) {
    return ((value >> bit) & 1) != 0;
}

uint BitExtract(uint value, uint offset, uint length) {
    const uint mask = (1u << length) - 1u;
    return (value >> offset) & mask;
}

uint ByteSwap16(uint val) {
    return ((val >> 8) & 0x00FF) |
           ((val << 8) & 0xFF00);
}

void WriteFB8(uint address, uint data) {
    const uint shift = (address & 3) * 8;
    const uint mask = ~(0xFF << shift);
    data = (data & 0xFF) << shift;

    address &= ~3;
    uint dummy;
    fbOut.InterlockedAnd(address, mask, dummy);
    fbOut.InterlockedOr(address, data, dummy);
}

void WriteMesh8(uint address, uint data) {
    WriteFB8(kMeshFBOffset + address, data);
}

// -----------------------------------------------------------------------------

static const bool deinterlace = BitTest(config.params, 29);
// NOTE: transparent meshes is assumed to be enabled
static const uint scale = BitExtract(config.scaling, 0, 3) + 1;

static const uint drawFB = BitExtract(config.params, 7, 1);
static const uint drawFBOffset = drawFB * kFBSize * scale * scale;
static const bool dblInterlaceEnable = BitTest(config.params, 4);

[numthreads(256, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint2 pos = id.xy;
    const uint address = drawFBOffset + id.x;

    if (BitTest(cpuWriteBitmap.Load(id.x >> 5), id.x & 31)) {
        WriteMesh8(address, 0);
        if (deinterlace && dblInterlaceEnable) {
            WriteMesh8(kDeinterlaceFBOffset + address, 0);
        }
    }
}
