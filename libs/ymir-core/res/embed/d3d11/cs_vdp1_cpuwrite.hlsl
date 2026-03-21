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

ByteAddressBuffer cpuWriteBitmap : register(t0);
ByteAddressBuffer fbram : register(t1);

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

// -----------------------------------------------------------------------------

static const uint kScaleBits = 12;
static const uint kScaleOne = 1 << kScaleBits;
static const uint kCoarseScaleBits = 6;
static const uint kCoarseScaleShift = kScaleBits - kCoarseScaleBits;

static const bool pixel8Bits = BitTest(config.params, 2);
static const uint offsetShift = BitExtract(config.params, 28, 1) + 8;
static const uint offsetMask = (1 << offsetShift) - 1;
static const bool deinterlace = BitTest(config.params, 29);
static const bool transparentMeshes = BitTest(config.params, 30);
static const uint scaleFactor = BitExtract(config.scale, 0, 16);
static const uint scaleStep = BitExtract(config.scale, 16, 16);
static const uint coarseScale = (scaleFactor + (1 << kCoarseScaleShift) - 1) >> kCoarseScaleShift;

uint ScaleUp(uint value) {
    return (value * scaleFactor) >> kScaleBits;
}

uint2 ScaleDown(uint2 value) {
    return (value * scaleStep) >> kScaleBits;
}

static const uint fbSizeH = ScaleUp(512 << BitExtract(config.params, 0, 1));
static const uint fbSizeV = ScaleUp(256 << BitExtract(config.params, 1, 1));

static const uint kFBSize = (((256 * 1024 * coarseScale) >> kCoarseScaleBits) * coarseScale) >> kCoarseScaleBits;
static const uint kDeinterlaceFBOffset = 2 * kFBSize;
static const uint kMeshFBOffset = 2 * 2 * kFBSize;

static const uint drawFB = BitExtract(config.params, 7, 1);
static const uint displayFB = drawFB ^ 1;
static const uint drawFBOffset = drawFB * kFBSize;
static const bool dblInterlaceEnable = BitTest(config.params, 4);

uint ReadFBUp8(uint address) {
    const uint value = fbram.Load(address & ~3);
    return (value >> ((address & 3) * 8)) & 0xFF;
}

uint ReadFBUp16(uint address) {
    const uint value = fbram.Load(address & ~3);
    return ByteSwap16(value >> ((address & 2) * 8));
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

void WriteFB16(uint address, uint data) {
    const uint shift = (address & 2) * 8;
    const uint mask = ~(0xFFFF << shift);
    data = ByteSwap16(data) << shift;

    address &= ~3;
    uint dummy;
    fbOut.InterlockedAnd(address, mask, dummy);
    fbOut.InterlockedOr(address, data, dummy);
}

void WriteMesh8(uint address, uint data) {
    WriteFB8(kMeshFBOffset + address, data);
}

void WriteMesh16(uint address, uint data) {
    WriteFB16(kMeshFBOffset + address, data);
}

[numthreads(32, 32, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= fbSizeH || id.y >= fbSizeV) {
        return;
    }
    const uint numBits = pixel8Bits ? 1 : 2;
    const uint posShift = pixel8Bits ? 0 : 1;

    const uint2 pos = ScaleDown(id.xy);
    const uint address = (pos.y * (1 << offsetShift) + pos.x) << posShift;

    const uint bits = BitExtract(cpuWriteBitmap.Load(address >> 3), address & 31, numBits);
    if (bits != 0) {
        uint inAddress = address;
        uint outAddress = drawFBOffset + ((id.y * ScaleUp(1 << offsetShift) + id.x) << posShift);

        if (bits == 3) {
            // 16-bit write
            const uint data = ReadFBUp16(inAddress);

            WriteFB16(outAddress, data);
            if (transparentMeshes) {
                WriteMesh16(outAddress, 0);
            }
            if (deinterlace && dblInterlaceEnable) {
                WriteFB16(kDeinterlaceFBOffset + outAddress, data);
                if (transparentMeshes) {
                    WriteMesh16(kDeinterlaceFBOffset + outAddress, 0);
                }
            }
        } else {
            // 8-bit write (upper or lower byte of 16-bit word, or pixel data is 8 bits)
            if (!pixel8Bits && bits == 2) {
                // Lower byte of 16-bit word
                inAddress++;
                outAddress++;
            }
            const uint data = ReadFBUp8(inAddress);

            WriteFB8(outAddress, data);
            if (transparentMeshes) {
                WriteMesh8(outAddress, 0);
            }
            if (deinterlace && dblInterlaceEnable) {
                WriteFB8(kDeinterlaceFBOffset + outAddress, data);
                if (transparentMeshes) {
                    WriteMesh8(kDeinterlaceFBOffset + outAddress, 0);
                }
            }
        }
    }
}
