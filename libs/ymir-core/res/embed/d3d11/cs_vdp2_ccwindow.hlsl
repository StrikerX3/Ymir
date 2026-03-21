struct Config {
    uint displayParams;
    uint startY;
    uint extraParams;
    uint vcellScrollParams;
    uint2 spriteParams;
    uint windows;
    uint fracScrollYBases;
    uint scale;
};

struct Window {
    uint2 start;
    uint2 end;
    uint lineWindowTableAddress;
    bool lineWindowTableEnable;
};

struct BGRenderState {
    uint4 nbgParams[4];
    uint4 rbgParams[2];

    uint2 nbgScrollAmount[4];
    uint2 nbgScrollInc[4];

    uint nbgPageBaseAddresses[4][4];
    uint rbgPageBaseAddresses[2][2][16];

    Window windows[2];

    uint commonRotParams;

    uint lineScreenParams;
    uint backScreenParams;

    uint specialFunctionCodes;
};

// -----------------------------------------------------------------------------

cbuffer Config : register(b0) {
    Config config;
}

ByteAddressBuffer vram : register(t0);
StructuredBuffer<BGRenderState> bgRenderState : register(t1);
Texture2DArray<uint4> bgIn : register(t2); // for sprite layer only

RWTexture2D<uint4> colorCalcWindowOut : register(u0);

// -----------------------------------------------------------------------------

static const uint kPixelAttrBitSpriteShadowWindow = 4;

static const uint kWindowLogicOR = 0;
static const uint kWindowLogicAND = 1;

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

uint ReadVRAM16(uint address) {
    return ByteSwap16(BitExtract(vram.Load(address & ~3), (address & 2) * 8, 16));
}

static const uint kInterlaceModeNone = 0;
static const uint kInterlaceModeInvalid = 1;
static const uint kInterlaceModeSingleDensity = 2;
static const uint kInterlaceModeDoubleDensity = 3;

static const uint interlaceMode = BitExtract(config.displayParams, 0, 2);
static const uint oddField = BitExtract(config.displayParams, 2, 1);
static const bool exclusiveMonitor = BitTest(config.displayParams, 3);
static const bool hiResH = BitTest(config.displayParams, 6);

static const bool deinterlace = BitTest(config.extraParams, 28);

uint GetY(uint y) {
    const bool interlaced = interlaceMode >= kInterlaceModeSingleDensity;

    if (!deinterlace && interlaced && !exclusiveMonitor) {
        return (y << 1) | oddField;
    } else {
        return y;
    }
}

bool InsideSpriteWindow(bool invert, uint2 pos) {
    return BitTest(bgIn[uint3(pos, 6)].a, kPixelAttrBitSpriteShadowWindow) != invert;
}

bool InsideWindow(Window window, bool invert, uint2 pos) {
    int2 start = window.start;
    int2 end = window.end;

    // Read line window if enabled
    if (window.lineWindowTableEnable) {
        const uint address = window.lineWindowTableAddress + pos.y * 4;
        start.x = ReadVRAM16(address + 0);
        end.x = ReadVRAM16(address + 2);
    }

    start.x = SignExtend(start.x, 16);
    end.x = SignExtend(end.x, 16);
    start.y = SignExtend(start.y, 16);
    end.y = SignExtend(end.y, 16);

    // Some games set out-of-range window parameters and expect them to work.
    // It seems like window coordinates should be signed...
    //
    // Panzer Dragoon 2 Zwei:
    //   0000 to FFFE -> empty window
    //   FFFE to 02C0 -> full line
    //
    // Panzer Dragoon Saga:
    //   0000 to FFFF -> empty window
    //
    // Snatcher:
    //   FFFC to 0286 -> full line
    //
    // Handle these cases here
    if (start.x < 0) {
        start.x = 0;
    }
    if (end.x < 0) {
        if (start.x >= end.x) {
            start.x = 0x3FF;
        }
        end.x = 0;
    }

    // For normal screen modes, X coordinates don't use bit 0
    if (!hiResH) {
        start.x >>= 1;
        end.x >>= 1;
    }

    const int2 spos = int2(pos);
    const bool inside = all(spos >= start) && all(spos <= end);
    return inside != invert;
}

// windowParams must contain:
// bit  use
//   0  Window logic
//   1  Window 0 enable
//   2  Window 0 invert
//   3  Window 1 enable
//   4  Window 1 invert
//   5  Sprite window enable
//   6  Sprite window invert
bool InsideWindows(uint windowParams, uint2 pos) {
    const Window windows[2] = bgRenderState[0].windows;
    const uint windowLogic = BitExtract(windowParams, 0, 1);
    const bool window0Enable = BitTest(windowParams, 1);
    const bool window0Invert = BitTest(windowParams, 2);
    const bool window1Enable = BitTest(windowParams, 3);
    const bool window1Invert = BitTest(windowParams, 4);
    const bool spriteWindowEnable = BitTest(windowParams, 5);
    const bool spriteWindowInvert = BitTest(windowParams, 6);

    // If no windows are enabled, consider the pixel outside of windows
    if (!window0Enable && !window1Enable && (!spriteWindowEnable)) {
        return false;
    }

    const bool windowLogicAND = windowLogic == kWindowLogicAND;

    bool inside = windowLogicAND;
    if (window0Enable) {
        const bool insideW0 = InsideWindow(windows[0], window0Invert, pos);
        if (windowLogicAND) {
            inside = inside && insideW0;
        } else {
            inside = inside || insideW0;
        }
    }
    if (window1Enable) {
        const bool insideW1 = InsideWindow(windows[1], window1Invert, pos);
        if (windowLogicAND) {
            inside = inside && insideW1;
        } else {
            inside = inside || insideW1;
        }
    }
    if (spriteWindowEnable) {
        const bool insideSW = InsideSpriteWindow(spriteWindowInvert, pos);
        if (windowLogicAND) {
            inside = inside && insideSW;
        } else {
            inside = inside || insideSW;
        }
    }

    return inside;
}

// -----------------------------------------------------------------------------

[numthreads(32, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint2 drawCoord = uint2(id.x, id.y + config.startY);
    const uint3 outCoord = uint3(drawCoord.x, GetY(drawCoord.y), id.z);
    colorCalcWindowOut[outCoord.xy] = InsideWindows(config.windows >> 5, drawCoord);
}
