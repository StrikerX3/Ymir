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

ByteAddressBuffer fbram : register(t0);

RWByteAddressBuffer fbOut : register(u0);

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

uint ReadFBRAM16(uint address) {
    return ByteSwap16(BitExtract(fbram.Load(address & ~3), (address & 2) * 8, 16));
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

// -----------------------------------------------------------------------------

static const uint fbSizeH = 512 << BitExtract(config.params, 0, 1);
static const bool vblankErase = BitTest(config.params, 7);
static const uint vblankEraseMaxY = BitExtract(config.params, 8, 9);
static const uint vblankEraseMaxX = BitExtract(config.params, 17, 10);
static const uint scaleV = BitExtract(config.erase, 31, 1);
static const uint eraseX1 = BitExtract(config.erase, 0, 6) << 3;
static const uint eraseY1 = BitExtract(config.erase, 6, 9) << scaleV;
static const uint eraseX3 = BitExtract(config.erase, 15, 7) << 3;
static const uint eraseY3 = BitExtract(config.erase, 22, 9) << scaleV;

[numthreads(32, 32, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint address = (id.x + id.y * fbSizeH) * 2;

    bool erasePixel = true;
    if (vblankErase) {
        if (id.y > vblankEraseMaxY) {
            erasePixel = false;
        } else if (id.y == vblankEraseMaxY && id.x > vblankEraseMaxX) {
            erasePixel = false;
        }
    } else if (id.x < eraseX1 || id.x >= eraseX3 || id.y < eraseY1 || id.y >= eraseY3) {
        erasePixel = false;

    }
    const uint eraseValue = erasePixel ? config.eraseWriteValue : ReadFBRAM16(address);

    WriteFB16(address, eraseValue);
}
