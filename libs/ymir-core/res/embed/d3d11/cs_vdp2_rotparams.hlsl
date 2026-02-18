struct Config {
    uint displayParams;
    uint startY;
    uint layerEnabled;
    uint _reserved;
};

struct RotParamBase {
    uint tableAddress;
    int Xst, Yst;
    uint KA;
};

struct RotParamState {
    int2 screenCoords;
    uint spriteCoords; // packed 2x int16
    uint coeffData; // bits 0-6 = raw line color data; bit 7 = transparency
};

// -----------------------------------------------------------------------------

cbuffer Config : register(b0) {
    Config config;
}

ByteAddressBuffer vram : register(t0);
ByteAddressBuffer cramCoeff : register(t1);
Buffer<uint2> rotParams : register(t2);
StructuredBuffer<RotParamBase> rotParamBases : register(t3);

RWStructuredBuffer<RotParamState> rotParamOut : register(u0);

// -----------------------------------------------------------------------------

static const uint kMaxNormalResH = 352;
static const uint kMaxNormalResV = 256;

static const uint kRotParamLinePitch = kMaxNormalResH;
static const uint kRotParamEntryStride = kRotParamLinePitch * kMaxNormalResV;

static const uint kCoeffDataModeScaleCoeffXY = 0;
static const uint kCoeffDataModeScaleCoeffX = 1;
static const uint kCoeffDataModeScaleCoeffY = 2;
static const uint kCoeffDataModeViewpointX = 3;

// -----------------------------------------------------------------------------

struct int64 {
    int hi;
    uint lo;
};

int64 i64_shr(int64 x, uint shift) {
    if (shift == 0) {
        return x;
    }
    int64 result;
    if (shift < 32) {
        result.hi = x.hi >> shift;
        result.lo = (x.lo >> shift) | (x.hi << (32 - shift));
    } else if (shift < 64) {
        result.hi = x.hi >> 31;
        result.lo = x.hi >> (shift - 32);
    } else {
        result.hi = result.lo = x.hi >> 31;
    }
    return result;
}

int64 i64_add(int64 x, int64 y) {
    int64 result;
    result.lo = x.lo + y.lo;
    result.hi = x.hi + y.hi;
    if (result.lo < x.lo) {
        ++result.hi;
    }
    return result;
}

int64 i64_mul32x32(int x, int y) {
    const uint xl = x & 0xFFFF;
    const int xh = x >> 16;
    const uint yl = y & 0xFFFF;
    const int yh = y >> 16;

    const uint p0 = xl * yl; // [0..31]
    const uint p1 = xh * yl; // [16..47]
    const uint p2 = xl * yh; // [16..47]
    const uint p3 = xh * yh; // [32..63]

    const int t = p1 + (p0 >> 16);
    const int w1 = (t & 0xFFFF) + p2;
    const int w2 = t >> 16;

    int64 result;
    result.lo = p0 + ((p1 + p2) << 16);
    result.hi = (w1 >> 16) + w2 + p3;
    return result;
}

int i64_mul32x32_mid32(int x, int y) {
    int64 result = i64_mul32x32(x, y);
    return (result.hi << 16) | (result.lo >> 16);
}

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

uint ReadVRAM16(uint address) {
    return ByteSwap16(BitExtract(vram.Load(address & ~3), (address & 2) * 8, 16));
}

// Expects address to be 32-bit-aligned
uint ReadVRAM32(uint address) {
    return ByteSwap32(vram.Load(address));
}

uint ReadCRAMCoeff16(uint address) {
    return ByteSwap16(BitExtract(cramCoeff.Load(address & ~3), (address & 2) * 8, 16));
}

// Expects address to be 32-bit-aligned
uint ReadCRAMCoeff32(uint address) {
    return ByteSwap32(cramCoeff.Load(address));
}

// -----------------------------------------------------------------------------

struct RotTable {
    // Screen start coordinates (signed 13.10 fixed point)
    int Xst, Yst, Zst;

    // Screen vertical coordinate increments (signed 3.10 fixed point)
    int deltaXst, deltaYst;

    // Screen horizontal coordinate increments (signed 3.10 fixed point)
    int deltaX, deltaY;

    // Rotation matrix parameters (signed 4.10 fixed point)
    int A, B, C, D, E, F;

    // Viewpoint coordinates (signed 14-bit integer)
    int Px, Py, Pz;

    // Center point coordinates (signed 14-bit integer)
    int Cx, Cy, Cz;

    // Horizontal shift (signed 14.10 fixed point)
    int Mx, My;

    // Scaling coefficients (signed 8.16 fixed point)
    int kx, ky;

    // Coefficient table parameters
    uint KAst; // Coefficient table start address (unsigned 16.10 fixed point)
    int dKAst; // Coefficient table vertical increment (signed 10.10 fixed point)
    int dKAx; // Coefficient table horizontal increment (signed 10.10 fixed point)

};

struct RotCoefficient {
    int value; // coefficient value, scaled to 16 fractional bits
    uint lineColorData;
    bool transparent;
};

RotTable ReadRotTable(const uint address) {
    RotTable table;
    
    table.Xst = SignExtend(ReadVRAM32(address + 0x00) >> 6, 23);
    table.Yst = SignExtend(ReadVRAM32(address + 0x04) >> 6, 23);
    table.Zst = SignExtend(ReadVRAM32(address + 0x08) >> 6, 23);

    table.deltaXst = SignExtend(ReadVRAM32(address + 0x0C) >> 6, 13);
    table.deltaYst = SignExtend(ReadVRAM32(address + 0x10) >> 6, 13);

    table.deltaX = SignExtend(ReadVRAM32(address + 0x14) >> 6, 13);
    table.deltaY = SignExtend(ReadVRAM32(address + 0x18) >> 6, 13);

    table.A = SignExtend(ReadVRAM32(address + 0x1C) >> 6, 14);
    table.B = SignExtend(ReadVRAM32(address + 0x20) >> 6, 14);
    table.C = SignExtend(ReadVRAM32(address + 0x24) >> 6, 14);
    table.D = SignExtend(ReadVRAM32(address + 0x28) >> 6, 14);
    table.E = SignExtend(ReadVRAM32(address + 0x2C) >> 6, 14);
    table.F = SignExtend(ReadVRAM32(address + 0x30) >> 6, 14);

    table.Px = SignExtend(ReadVRAM16(address + 0x34), 14);
    table.Py = SignExtend(ReadVRAM16(address + 0x36), 14);
    table.Pz = SignExtend(ReadVRAM16(address + 0x38), 14);

    table.Cx = SignExtend(ReadVRAM16(address + 0x3C), 14);
    table.Cy = SignExtend(ReadVRAM16(address + 0x3E), 14);
    table.Cz = SignExtend(ReadVRAM16(address + 0x40), 14);

    table.Mx = SignExtend(ReadVRAM32(address + 0x44) >> 6, 24);
    table.My = SignExtend(ReadVRAM32(address + 0x48) >> 6, 24);

    table.kx = SignExtend(ReadVRAM32(address + 0x4C), 24);
    table.ky = SignExtend(ReadVRAM32(address + 0x50), 24);

    table.KAst = ReadVRAM32(address + 0x54) >> 6;
    table.dKAst = SignExtend(ReadVRAM32(address + 0x58) >> 6, 20);
    table.dKAx = SignExtend(ReadVRAM32(address + 0x5C) >> 6, 20);
    
    return table;
}

bool CanFetchCoefficient(uint2 rotParams, uint coeffAddress) {
    const bool coeffTableCRAM = BitTest(rotParams.x, 1);
    if (coeffTableCRAM) {
        return true;
    }

    const bool coeffDataPerDot = BitTest(rotParams.x, 9);
    if (!coeffDataPerDot) {
        return true;
    }

    const uint coeffDataSize = BitExtract(rotParams.x, 2, 1);
    const uint coeffDataAccess = BitExtract(rotParams.x, 5, 4);
    const uint offset = coeffAddress >> 10u;
    const uint address = (offset * 4) >> coeffDataSize;
    const uint bank = BitExtract(address, 17, 2);
    return BitTest(coeffDataAccess, bank);
}

RotCoefficient ReadRotCoefficient(uint2 rotParams, uint coeffAddress) {
    const uint offset = coeffAddress >> 10;
    const bool coeffTableCRAM = BitTest(rotParams.x, 1);
    const bool coeffDataSize = BitTest(rotParams.x, 2);
    const uint coeffDataMode = BitExtract(rotParams.x, 3, 2);

    RotCoefficient coeff;
    
    // Force coefficient to 0 if it cannot be read in per-dot mode
    if (!CanFetchCoefficient(rotParams, coeffAddress)) {
        coeff.value = 0;
        coeff.lineColorData = 0;
        coeff.transparent = true;
        return coeff;
    }
    
    if (coeffDataSize) {
        // One-word coefficient data
        const uint address = offset * 2;
        const uint data = coeffTableCRAM ? ReadCRAMCoeff16(address) : ReadVRAM16(address);
        coeff.value = SignExtend(data, 15);
        coeff.lineColorData = 0;
        coeff.transparent = BitTest(data, 15);

        if (coeffDataMode == kCoeffDataModeViewpointX) {
            coeff.value <<= 14;
        } else {
            coeff.value <<= 6;
        }
    } else {
        // Two-word coefficient data
        const uint address = offset * 4;
        const uint data = coeffTableCRAM ? ReadCRAMCoeff32(address) : ReadVRAM32(address);
        coeff.value = SignExtend(data, 24);
        coeff.lineColorData = BitExtract(data, 24, 7);
        coeff.transparent = BitTest(data, 31);

        if (coeffDataMode == kCoeffDataModeViewpointX) {
            coeff.value <<= 8;
        }
    }

    return coeff;
}

RotParamState CalcRotation(uint2 pos, uint index) {
    const RotParamBase base = rotParamBases[index];
    const uint2 rotParam = rotParams[index];
    
    const bool coeffTableEnable = BitTest(rotParam.x, 0);
    const uint coeffDataMode = BitExtract(rotParam.x, 3, 2);
    const bool coeffDataPerDot = BitTest(rotParam.x, 9);
    const bool fbRotEnable = BitTest(rotParam.x, 10);
    
    const RotTable t = ReadRotTable(base.tableAddress);

    int Tx, Ty, Tz;
    
    // Common terms for Xsp and Ysp (14.10)
    // 10 - 0 = 10 frac bits
    // 23 - 14 = 23 total bits
    // expand to 10 frac bits
    // 10 - 10 = 10 frac bits
    // 23 - 24 = 24 total bits
    const int Xst = base.Xst + pos.y * t.deltaXst;
    const int Yst = base.Yst + pos.y * t.deltaYst;
    const int Zst = t.Zst;
    Tx = Xst - (t.Px << 10);
    Ty = Yst - (t.Py << 10);
    Tz = Zst - (t.Pz << 10);

    // Transformed starting screen coordinates (18.10)
    // 10*(10-10) + 10*(10-10) + 10*(10-10) = 20 frac bits
    // 14*(23-24) + 14*(23-24) + 14*(23-24) = 38 total bits
    // reduce to 10 frac bits
    const int Xsp = i64_shr(i64_add(i64_add(i64_mul32x32(t.A, Tx), i64_mul32x32(t.B, Ty)), i64_mul32x32(t.C, Tz)), 10).lo;
    const int Ysp = i64_shr(i64_add(i64_add(i64_mul32x32(t.D, Tx), i64_mul32x32(t.E, Ty)), i64_mul32x32(t.F, Tz)), 10).lo;

    // Transformed view coordinates (18.10)
    // 10*(0-0) + 10*(0-0) + 10*(0-0) + 10 + 10 = 10+10+10 + 10+10 = 10 frac bits
    // 14*(14-14) + 14*(14-14) + 14*(14-14) + 24 + 24 = 28+28+28 + 24+24 = 28 total bits
    Tx = t.Px - t.Cx;
    Ty = t.Py - t.Cy;
    Tz = t.Pz - t.Cz;
    int /***/ Xp = t.A * Tx + t.B * Ty + t.C * Tz + (t.Cx << 10) + t.Mx;
    const int Yp = t.D * Tx + t.E * Ty + t.F * Tz + (t.Cy << 10) + t.My;

    // Screen coordinate increments per Hcnt (7.10)
    // 10*10 + 10*10 = 20 + 20 = 20 frac bits
    // 14*13 + 14*13 = 27 + 27 = 27 total bits
    // reduce to 10 frac bits
    const int scrXIncH = (t.A * t.deltaX + t.B * t.deltaY) >> 10;
    const int scrYIncH = (t.D * t.deltaX + t.E * t.deltaY) >> 10;
    
    int kx = t.kx;
    int ky = t.ky;

    RotCoefficient coeff;
    if (coeffTableEnable) {
        // Current coefficient address (16.10)
        const uint KAxofs = coeffDataPerDot ? pos.x * t.dKAx : 0;
        const uint KA = base.KA + pos.y * t.dKAst + KAxofs;

        // Read and apply rotation coefficient
        coeff = ReadRotCoefficient(rotParam, KA);
   
        switch (coeffDataMode) {
            case kCoeffDataModeScaleCoeffXY:
                kx = ky = coeff.value;
                break;
            case kCoeffDataModeScaleCoeffX:
                kx = coeff.value;
                break;
            case kCoeffDataModeScaleCoeffY:
                ky = coeff.value;
                break;
            case kCoeffDataModeViewpointX:
                Xp = coeff.value << 2;
                break;
        }
    } else {
        coeff.value = 0;
        coeff.lineColorData = 0;
        coeff.transparent = true;
    }
    
    RotParamState result;
    
    // Current screen coordinates (18.10)
    const int scrX = Xsp + pos.x * scrXIncH;
    const int scrY = Ysp + pos.x * scrYIncH;

    // Resulting screen coordinates (26.0)
    // (16*10) + 10 = 26 + 10 frac bits
    // (24*28) + 28 = 52 + 28 total bits
    // reduce 26 to 10 frac bits
    // = 10 + 10 = 10 frac bits
    // = 36 + 28 = 36 total bits
    // remove frac bits from result = 26 total bits
    result.screenCoords = int2(
        (i64_mul32x32_mid32(kx, scrX) + Xp) >> 10,
        (i64_mul32x32_mid32(ky, scrY) + Yp) >> 10
    );
    
    if (fbRotEnable) {
        // Current sprite coordinates (13.10)
        // 10 + 0*10 + 0*10 = 10 + 10 + 10 = 10 frac bits
        // 23 + 10*13 + 9*13 = 23 + 23 + 22 = 23 total bits
        const int sprX = t.Xst + pos.y * t.deltaXst + pos.x * t.deltaX;
        const int sprY = t.Yst + pos.y * t.deltaYst + pos.x * t.deltaY;

        // Pack resulting sprite coordinates (13.0)
        result.spriteCoords = ((sprX >> 10) & 0xFFFF) | ((sprY >> 10) << 16);
    } else {
        result.spriteCoords = 0;
    }
    
    // Pack coefficient data
    result.coeffData = coeff.lineColorData | (coeff.transparent << 7);
    
    return result;
}

[numthreads(32, 1, 2)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint outIndex = id.x + (id.y + config.startY) * kRotParamLinePitch + id.z * kRotParamEntryStride;
    rotParamOut[outIndex] = CalcRotation(id.xy, id.z);
}
