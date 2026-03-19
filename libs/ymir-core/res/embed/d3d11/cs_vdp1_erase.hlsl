struct Config {
    uint params;
    uint erase;
    uint eraseWriteValue;
    uint scale;
};

// -----------------------------------------------------------------------------

cbuffer Config : register(b0) {
    Config config;
}

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

static const uint kScaleBits = 12;
static const uint kScaleOne = 1 << kScaleBits;
static const uint kCoarseScaleBits = 6;
static const uint kCoarseScaleShift = kScaleBits - kCoarseScaleBits;

#ifndef YMIR_BYPASS_ENHANCEMENTS
static const bool deinterlace = BitTest(config.params, 29);
static const bool transparentMeshes = BitTest(config.params, 30);
static const uint scale = BitExtract(config.scale, 0, 16);
static const uint coarseScale = (scale + (1 << kCoarseScaleShift) - 1) >> kCoarseScaleShift;
static const uint kFBSize = (((256 * 1024 * coarseScale) >> kCoarseScaleBits) * coarseScale) >> kCoarseScaleBits;
#else
static const bool deinterlace = false;
static const bool transparentMeshes = false;
static const uint scale = kScaleOne;
static const uint kFBSize = 256 * 1024;
#endif

static const uint kDeinterlaceFBOffset = 2 * kFBSize;
static const uint kMeshFBOffset = 2 * 2 * kFBSize;

#ifndef YMIR_BYPASS_ENHANCEMENTS
void WriteMesh16(uint address, uint data) {
    WriteFB16(kMeshFBOffset + address, data);
}

uint ScaleUp(uint value) {
    return (value * scale) >> kScaleBits;
}

uint ScaleUpBiasCeil(uint value) {
    return (value * scale + scale - 1) >> kScaleBits;
}

uint ScaleUpPlusOne(uint value) {
    return (value * scale + scale) >> kScaleBits;
}
#else
uint ScaleUp(uint value) {
    return value;
}

uint ScaleUpBiasCeil(uint value) {
    return value;
}

uint ScaleUpPlusOne(uint value) {
    return value;
}
#endif

static const uint drawFB = BitExtract(config.params, 7, 1);
static const uint drawFBOffset = drawFB * kFBSize;
static const bool vblankErase = BitTest(config.params, 8);
static const uint vblankEraseMaxY = ScaleUpBiasCeil(BitExtract(config.params, 9, 9));
static const uint vblankEraseMaxX = ScaleUpBiasCeil(BitExtract(config.params, 18, 10));
static const uint offsetShift = BitExtract(config.params, 28, 1) + 8;
static const uint eraseScaleV = BitExtract(config.erase, 31, 1);
static const uint eraseX1 = ScaleUp(BitExtract(config.erase, 0, 6) << 3);
static const uint eraseY1 = ScaleUp(BitExtract(config.erase, 6, 9) << eraseScaleV);
static const uint eraseX3 = ScaleUpPlusOne(BitExtract(config.erase, 15, 7) << 3);
static const uint eraseY3 = ScaleUpPlusOne(BitExtract(config.erase, 22, 9) << eraseScaleV);
static const bool dblInterlaceEnable = BitTest(config.params, 4);

[numthreads(32, 32, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint2 pos = uint2(id.x + eraseX1, id.y + eraseY1);
    const uint address = drawFBOffset + (id.y * ScaleUp(1 << offsetShift) + id.x) * 2;

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
#ifndef YMIR_BYPASS_ENHANCEMENTS
    if (transparentMeshes) {
        WriteMesh16(address, 0);
    }
    if (deinterlace && dblInterlaceEnable) {
        WriteFB16(kDeinterlaceFBOffset + address, config.eraseWriteValue);
        if (transparentMeshes) {
            WriteMesh16(kDeinterlaceFBOffset + address, 0);
        }
    }
#endif
}
