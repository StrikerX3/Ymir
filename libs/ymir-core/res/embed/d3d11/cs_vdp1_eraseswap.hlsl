struct Config {
    uint params;
    uint erase;
    uint eraseWriteValue;
    uint _reserved;
};

// -----------------------------------------------------------------------------

cbuffer Config : register(b0) {
    Config config;
}

RWByteAddressBuffer fbOut : register(u0);
RWByteAddressBuffer meshOut : register(u1);

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

void WriteFB16(uint address, uint data) {
    const uint shift = (address & 2) * 8;
    const uint mask = ~(0xFFFF << shift);
    data = ByteSwap16(data) << shift;

    address &= ~3;
    uint dummy;
    fbOut.InterlockedAnd(address, mask, dummy);
    fbOut.InterlockedOr(address, data, dummy);
}

void WriteMesh16(uint address, uint data) {
    const uint shift = (address & 2) * 8;
    const uint mask = ~(0xFFFF << shift);
    data = ByteSwap16(data) << shift;

    address &= ~3;
    uint dummy;
    meshOut.InterlockedAnd(address, mask, dummy);
    meshOut.InterlockedOr(address, data, dummy);
}

// -----------------------------------------------------------------------------

static const uint drawFB = BitExtract(config.params, 7, 1);
static const uint drawFBOffset = drawFB * 256 * 1024;
static const bool vblankErase = BitTest(config.params, 8);
static const uint vblankEraseMaxY = BitExtract(config.params, 9, 9);
static const uint vblankEraseMaxX = BitExtract(config.params, 18, 10);
static const uint offsetShift = BitExtract(config.params, 28, 1) + 8;
static const uint scaleV = BitExtract(config.erase, 31, 1);
static const uint eraseX1 = BitExtract(config.erase, 0, 6) << 3;
static const uint eraseY1 = BitExtract(config.erase, 6, 9) << scaleV;
static const uint eraseX3 = BitExtract(config.erase, 15, 7) << 3;
static const uint eraseY3 = BitExtract(config.erase, 22, 9) << scaleV;
static const bool transparentMeshes = BitTest(config.params, 30);

[numthreads(32, 32, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint2 pos = uint2(id.x + eraseX1, id.y + eraseY1);
    const uint address = drawFBOffset + ((id.y << offsetShift) + id.x) * 2;

    // Bail out if out of range
    if (id.x >= eraseX3 - eraseX1 + 1 || id.y > eraseY3 - eraseY1 + 1) {
        return;
    }
    // Bail out if pixel exceeds VBlank erase cycle limit
    if (vblankErase) {
        if (id.y > vblankEraseMaxY) {
            return;
        }
        if (id.y == vblankEraseMaxY && id.x > vblankEraseMaxX) {
            return;
        }
    }

    WriteFB16(address, config.eraseWriteValue);
    if (transparentMeshes) {
        WriteMesh16(address, 0);
    }
}
