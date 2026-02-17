struct Config {
    uint displayParams;
    uint startY;
    uint2 _padding;
};

struct Window {
    uint2 start;
    uint2 end;
    uint lineWindowTableAddress;
    bool lineWindowTableEnable;
};

struct RenderState {
    uint4 nbgParams[4];
    uint2 rbgParams[2];
    
    uint2 nbgScrollAmount[4];
    uint2 nbgScrollInc[4];
    
    uint nbgPageBaseAddresses[4][4];
    uint rbgPageBaseAddresses[2][16];

    // TODO: NBG line scroll offset tables (X/Y) (or addresses to read from VRAM)
    // TODO: Vertical cell scroll table base address
    // TODO: Mosaic sizes (X/Y)
    // TODO: Sprite layer parameters

    Window windows[2];

    uint specialFunctionCodes;
    uint3 _padding;
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
StructuredBuffer<uint> cram : register(t1);
StructuredBuffer<RenderState> renderState : register(t2);
StructuredBuffer<RotParamState> rotParamState : register(t3);

RWTexture2DArray<uint4> bgOut : register(u0);

// -----------------------------------------------------------------------------

static const uint kWindowLogicOR = 0;
static const uint kWindowLogicAND = 1;

static const uint kColorFormatPalette16 = 0;
static const uint kColorFormatPalette256 = 1;
static const uint kColorFormatPalette2048 = 2;
static const uint kColorFormatRGB555 = 3;
static const uint kColorFormatRGB888 = 4;

static const uint kPriorityModeScreen = 0;
static const uint kPriorityModeCharacter = 1;
static const uint kPriorityModeDot = 2;

static const uint kSpecColorCalcModeScreen = 0;
static const uint kSpecColorCalcModeCharacter = 1;
static const uint kSpecColorCalcModeDot = 2;
static const uint kSpecColorCalcModeColorMSB = 3;

static const uint kPageSizes[2][2] = { { 13, 14 }, { 11, 12 } };

static const uint kCRAMAddressMask = ((config.displayParams >> 3) & 3) == 1 ? 0x7FF : 0x3FF;

static const uint kMaxNormalResH = 352;
static const uint kMaxNormalResV = 256;

static const uint kRotParamLinePitch = kMaxNormalResH;
static const uint kRotParamEntryStride = kRotParamLinePitch * kMaxNormalResV;

struct Character {
    uint charNum;
    uint palNum;
    bool specColorCalc;
    bool specPriority;
    bool flipH;
    bool flipV;
};

static const Character kBlankCharacter = (Character) 0;
static const uint4 kTransparentPixel = uint4(0, 0, 0, 128);

// -----------------------------------------------------------------------------

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

uint ReadVRAM4(uint address, uint nibble) {
    return (vram.Load(address & ~3) >> ((address & 3) * 8 + nibble * 4)) & 0xF;
}

uint ReadVRAM8(uint address) {
    return vram.Load(address & ~3) >> ((address & 3) * 8) & 0xFF;
}

uint ReadVRAM16(uint address) {
    return ByteSwap16(vram.Load(address & ~3) >> ((address & 2) * 8));
}

// Expects address to be 32-bit-aligned
uint ReadVRAM32(uint address) {
    return ByteSwap32(vram.Load(address));
}

uint GetY(uint y) {
    const bool interlaced = config.displayParams & 1;
    const bool odd = (config.displayParams >> 1) & 1;
    const bool exclusiveMonitor = (config.displayParams >> 2) & 1;
    if (interlaced && !exclusiveMonitor) {
        return (y << 1) | (odd /* TODO & !deinterlace */);
    } else {
        return y;
    }
}

uint4 FetchCRAMColor(uint cramOffset, uint colorIndex) {
    const uint cramAddress = (cramOffset + colorIndex) & kCRAMAddressMask;
    const uint cramValue = cram[cramAddress];
    return uint4(cramValue & 0xFF, (cramValue >> 8) & 0xFF, (cramValue >> 16) & 0xFF, (cramValue >> 24) & 0xFF);
}

uint4 Color555(uint val16) {
    return uint4(
        ((val16 >> 0) & 0x1F) << 3,
        ((val16 >> 5) & 0x1F) << 3,
        ((val16 >> 10) & 0x1F) << 3,
        (val16 >> 15) & 1
    );
}

uint4 Color888(uint val32) {
    return uint4(
        (val32 >> 0) & 0xFF,
        (val32 >> 8) & 0xFF,
        (val32 >> 16) & 0xFF,
        (val32 >> 31) & 1
    );
}

bool InsideWindow(Window window, bool invert, uint2 pos) {
    const bool hiResH = (config.displayParams >> 5) & 1;
    
    int2 start = window.start;
    int2 end = window.end;
    
    // Read line window if enabled
    if (window.lineWindowTableEnable) {
        const uint address = window.lineWindowTableAddress + pos.y * 4;
        start.x = ReadVRAM16(address + 0);
        end.x = ReadVRAM16(address + 2);
    }
    
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
    if (hiResH == 0) {
        start.x >>= 1;
        end.x >>= 1;
    }
    
    const bool inside = int(pos.x) >= start.x && int(pos.y) >= start.y &&
                        int(pos.x) <= end.x && int(pos.y) <= end.y;
    return inside != invert;
}

bool InsideWindows(uint4 nbgParams, uint2 pos) {
    const Window windows[2] = renderState[0].windows;
    const bool window0Enable = (nbgParams.y >> 13) & 1;
    const bool window0Invert = (nbgParams.y >> 14) & 1;
    const bool window1Enable = (nbgParams.y >> 15) & 1;
    const bool window1Invert = (nbgParams.y >> 16) & 1;
    const bool spriteWindowEnable = (nbgParams.y >> 17) & 1;
    // TODO: const bool spriteWindowInvert = (nbgParams.y >> 18) & 1;
    const uint windowLogic = (nbgParams.y >> 19) & 1;
    
    // If no windows are enabled, consider the pixel outside of windows
    if (!window0Enable && !window1Enable && !spriteWindowEnable) {
        return false;
    }
    
    const bool insideW0 = window0Enable && InsideWindow(windows[0], window0Invert, pos);
    const bool insideW1 = window1Enable && InsideWindow(windows[1], window1Invert, pos);
    const bool insideSW = spriteWindowEnable && false; // TODO: InsideSpriteWindow(...);
    
    if (windowLogic == kWindowLogicAND) {
        return insideW0 && insideW1 /*&& insideSW*/;
    } else {
        return insideW0 || insideW1 /*|| insideSW*/;
    }
    
    return false;
}

bool IsSpecialColorCalcMatch(uint4 nbgParams, uint specColorCode) {
    const uint specFuncSelect = (nbgParams.x >> 16) & 1;
    return (renderState[0].specialFunctionCodes >> (specFuncSelect * 8 + specColorCode)) & 1;
}

bool GetSpecialColorCalcFlag(uint4 nbgParams, uint specColorCode, bool specColorCalc, bool colorMSB) {
    const bool colorCalcEnable = (nbgParams.x >> 29) & 1;
    if (!colorCalcEnable) {
        return false;
    }
    
    const uint specColorCalcMode = (nbgParams.x >> 14) & 3;
    switch (specColorCalcMode) {
        case kSpecColorCalcModeScreen:
            return colorCalcEnable;
        case kSpecColorCalcModeCharacter:
            return specColorCalc;
        case kSpecColorCalcModeDot:
            return specColorCalc && IsSpecialColorCalcMatch(nbgParams, specColorCode);
        case kSpecColorCalcModeColorMSB:
            return colorMSB;
    }
}

Character FetchTwoWordCharacter(uint4 nbgParams, uint pageAddress, uint charIndex) {
    const uint patNameAccess = nbgParams.w & 0xF;
    const uint charAddress = pageAddress + charIndex * 4;
    const uint charBank = (charAddress >> 17) & 3;
 
    if (((patNameAccess >> charBank) & 1) == 0) {
        return kBlankCharacter;
    }
    
    const uint charData = ReadVRAM32(charAddress);

    Character ch;
    ch.charNum = charData & 0x7FFF;
    ch.palNum = ((charData >> 16) & 0x7F) << 4;
    ch.specColorCalc = (charData >> 28) & 1;
    ch.specPriority = (charData >> 29) & 1;
    ch.flipH = (charData >> 30) & 1;
    ch.flipV = (charData >> 31) & 1;
    return ch;
}

Character FetchOneWordCharacter(uint4 nbgParams, uint pageAddress, uint charIndex) {
    const uint charAddress = pageAddress + charIndex * 2;
    const uint charBank = (charAddress >> 17) & 3;
    const uint patNameAccess = nbgParams.w & 0xF;
    if (((patNameAccess >> charBank) & 1) == 0) {
        return kBlankCharacter;
    }
    
    const uint charData = ReadVRAM16(charAddress);

    const uint supplScrollCharNum = (nbgParams.w >> 9) & 0x1F;
    const uint supplScrollPalNum = ((nbgParams.x >> 22) & 7) << 4;
    const bool supplScrollSpecialColorCalc = (nbgParams.x >> 25) & 1;
    const bool supplScrollSpecialPriority = (nbgParams.x >> 26) & 1;
    const bool extChar = (nbgParams.w >> 6) & 1;
    const bool cellSizeShift = (nbgParams.w >> 8) & 1;
    const uint colorFormat = (nbgParams.x >> 11) & 7;
    
    // Character number bit range from the 1-word character pattern data (charData)
    const uint baseCharNumMask = extChar ? 0xFFF : 0x3FF;
    const uint baseCharNumPos = 2 * cellSizeShift;

    // Upper character number bit range from the supplementary character number (bgParams.supplCharNum)
    const uint supplCharNumStart = 2 * cellSizeShift + 2 * extChar;
    const uint supplCharNumMask = 0xF >> supplCharNumStart;
    const uint supplCharNumPos = 10 + supplCharNumStart;
    // The lower bits are always in range 0..1 and only used if cellSizeShift == true

    const uint baseCharNum = charData & baseCharNumMask;
    const uint supplCharNum = (supplScrollCharNum >> supplCharNumStart) & supplCharNumMask;

    Character ch;
    ch.charNum = (baseCharNum << baseCharNumPos) | (supplCharNum << supplCharNumPos);
    if (cellSizeShift) {
        ch.charNum |= supplScrollCharNum & 3;
    }
    if (colorFormat != kColorFormatPalette16) {
        ch.palNum = ((charData >> 12) & 7) << 8;
    } else {
        ch.palNum = (((charData >> 12) & 0xF) | (supplScrollPalNum << 4)) << 4;
    }
    ch.specColorCalc = supplScrollSpecialColorCalc;
    ch.specPriority = supplScrollSpecialPriority;
    ch.flipH = !extChar && ((charData >> 10) & 1);
    ch.flipV = !extChar && ((charData >> 11) & 1);
    return ch;
}

uint4 FetchPixel(uint4 nbgParams, uint baseAddress, uint2 dotPos, uint linePitch, uint palNum, bool specColorCalc, uint specPriority) {
    const uint charPatAccess = nbgParams.x & 0xF;
    const uint colorFormat = (nbgParams.x >> 11) & 7;
    const uint cramOffset = nbgParams.x & 0x700;
    const uint bgPriorityNum = (nbgParams.x >> 17) & 7;
    const uint bgPriorityMode = (nbgParams.x >> 20) & 3;
    const bool enableTransparency = (nbgParams.x >> 28) & 1;
  
    const uint dotOffset = dotPos.x + dotPos.y * linePitch;
    
    uint colorData;
    uint4 outColor;
    bool outTransparent;
    bool outSpecColorCalc;
    if (colorFormat == kColorFormatPalette16) {
        const uint dotAddress = baseAddress + (dotOffset >> 1);
        const uint dotBank = (dotAddress >> 17) & 3;
        const uint dotData = ((charPatAccess >> dotBank) & 1) ? ReadVRAM4(dotAddress, ~dotPos.x & 1) : 0;
        const uint colorIndex = palNum | dotData;
        colorData = (dotData >> 1) & 7;
        outColor = FetchCRAMColor(cramOffset, colorIndex);
        outTransparent = enableTransparency && dotData == 0;
        outSpecColorCalc = GetSpecialColorCalcFlag(nbgParams, colorData, specColorCalc, outColor.a);

    } else if (colorFormat == kColorFormatPalette256) {
        const uint dotAddress = baseAddress + dotOffset;
        const uint dotBank = (dotAddress >> 17) & 3;
        const uint dotData = ((charPatAccess >> dotBank) & 1) ? ReadVRAM8(dotAddress) : 0;
        const uint colorIndex = (palNum & 0x700) | dotData;
        colorData = (dotData >> 1) & 7;
        outColor = FetchCRAMColor(cramOffset, colorIndex);
        outTransparent = enableTransparency && dotData == 0;
        outSpecColorCalc = GetSpecialColorCalcFlag(nbgParams, colorData, specColorCalc, outColor.a);

    } else if (colorFormat == kColorFormatPalette2048) {
        const uint dotAddress = baseAddress + (dotOffset << 1);
        const uint dotBank = (dotAddress >> 17) & 3;
        const uint dotData = ((charPatAccess >> dotBank) & 1) ? ReadVRAM16(dotAddress) : 0;
        const uint colorIndex = dotData & 0x7FF;
        colorData = (dotData >> 1) & 7;
        outColor = FetchCRAMColor(cramOffset, colorIndex);
        outTransparent = enableTransparency && (dotData & 0x7FF) == 0;
        outSpecColorCalc = GetSpecialColorCalcFlag(nbgParams, colorData, specColorCalc, outColor.a);

    } else if (colorFormat == kColorFormatRGB555) {
        const uint dotAddress = baseAddress + (dotOffset << 1);
        const uint dotBank = (dotAddress >> 17) & 3;
        const uint dotData = ((charPatAccess >> dotBank) & 1) ? ReadVRAM16(dotAddress) : 0;
        outColor = Color555(dotData);
        outTransparent = enableTransparency && outColor.w == 0;
        outSpecColorCalc = GetSpecialColorCalcFlag(nbgParams, 7, specColorCalc, true);

    } else if (colorFormat == kColorFormatRGB888) {
        const uint dotAddress = baseAddress + (dotOffset << 2);
        const uint dotBank = (dotAddress >> 17) & 3;
        const uint dotData = ((charPatAccess >> dotBank) & 1) ? ReadVRAM32(dotAddress) : 0;
        outColor = Color888(dotData);
        outTransparent = enableTransparency && outColor.w == 0;
        outSpecColorCalc = GetSpecialColorCalcFlag(nbgParams, 7, specColorCalc, true);

    } else {
        colorData = 0;
        outColor = uint4(0, 0, 0, 0);
        outTransparent = true;
        outSpecColorCalc = false;
    }
    
    uint outPriority = bgPriorityNum;
    if (bgPriorityMode == kPriorityModeCharacter) {
        outPriority &= ~1;
        outPriority |= specPriority;
    } else if (bgPriorityMode == kPriorityModeDot) {
        outPriority &= ~1;
        if (specPriority && colorFormat < kColorFormatRGB555) {
            outPriority |= IsSpecialColorCalcMatch(nbgParams, colorData);
        }
    }
    
    return uint4(outColor.xyz, (outTransparent << 7) | (outSpecColorCalc << 6) | outPriority);
}

uint4 FetchCharacterPixel(uint4 nbgParams, Character ch, uint2 dotPos, uint cellIndex) {
    const bool cellSizeShift = (nbgParams.w >> 8) & 1;
    const uint colorFormat = (nbgParams.x >> 11) & 7;

    if (ch.flipH) {
        dotPos.x ^= 7;
        if (cellSizeShift > 0) {
            cellIndex ^= 1;
        }
    }
    if (ch.flipV) {
        dotPos.y ^= 7;
        if (cellSizeShift > 0) {
            cellIndex ^= 2;
        }
    }
        
    // Adjust cell index based on color format
    if (colorFormat == kColorFormatRGB888) {
        cellIndex <<= 3;
    } else if (colorFormat == kColorFormatRGB555) {
        cellIndex <<= 2;
    } else if (colorFormat != kColorFormatPalette16) {
        cellIndex <<= 1;
    }

    const uint baseAddress = (ch.charNum + cellIndex) << 5;
    const bool specColorCalc = ch.specColorCalc;
    const uint specPriority = ch.specPriority;

    return FetchPixel(nbgParams, baseAddress, dotPos, 8, ch.palNum, specColorCalc, specPriority);
}

uint4 FetchBitmapPixel(uint4 nbgParams, uint2 scrollPos) {
    const uint bitmapSizeH = 512 << (nbgParams.w & 1);
    const uint bitmapSizeV = 256 << ((nbgParams.w >> 1) & 1);

    const uint2 dotPos = scrollPos & uint2(bitmapSizeH - 1, bitmapSizeV - 1);
    const uint baseAddress = ((nbgParams.w >> 2) & 7) << 17;
    const uint palNum = ((nbgParams.x >> 22) & 7) << 8;
    const bool specColorCalc = (nbgParams.x >> 25) & 1;
    const uint specPriority = (nbgParams.x >> 26) & 1;
 
    return FetchPixel(nbgParams, baseAddress, dotPos, bitmapSizeH, palNum, specColorCalc, specPriority);
}

uint4 DrawScrollNBG(uint2 pos, uint index) {
    const RenderState state = renderState[0];
    const uint4 nbgParams = state.nbgParams[index];

    if (InsideWindows(nbgParams, pos)) {
        return kTransparentPixel;
    }
  
    const uint2 pageShift = uint2((nbgParams.w >> 4) & 1, (nbgParams.w >> 5) & 1);
    const bool twoWordChar = (nbgParams.w >> 7) & 1;
    const bool cellSizeShift = (nbgParams.w >> 8) & 1;
    const uint pageSize = kPageSizes[cellSizeShift][twoWordChar];
    
    pos.y = GetY(pos.y);
    
    const uint2 fracScrollPos = state.nbgScrollAmount[index] + state.nbgScrollInc[index] * pos;

    // TODO: mosaic, line screen scroll, vertical cell scroll, data access delays, etc.
    // const bool lineZoomEnable = nbgParams.y & 1;
    // const bool lineScrollXEnable = (nbgParams.y >> 1) & 1;
    // const bool lineScrollYEnable = (nbgParams.y >> 2) & 1;
    // const uint lineScrollInterval = 1 << ((nbgParams.y >> 3) & 3);
    // const uint lineScrollTableAddress = ((nbgParams.y >> 5) & 7) << 17;
    // const bool vertCellScrollEnable = (nbgParams.y >> 8) & 1;
    // const bool vertCellScrollDelay = (nbgParams.y >> 9) & 1;
    // const uint vertCellScrollOffset = ((nbgParams.y >> 10) & 1) << 2;
    // const bool vertCellScrollRepeat = (nbgParams.y >> 11) & 1;
    // const bool mosaicEnable = (nbgParams.x >> 12) & 1;
    
    const uint2 scrollPos = fracScrollPos >> 8;
  
    const uint2 planePos = (scrollPos >> (pageShift + 9)) & 1;
    const uint plane = planePos.x | (planePos.y << 1);
    const uint pageBaseAddress = state.nbgPageBaseAddresses[index][plane];
    
    // TODO: apply data access shift hack here
    // const uint bank = (pageBaseAddress >> 17) & 3;
    // const uint vramAccessOffset = ((nbgParams.x >> (4 + bank)) & 1) << 3;
    
    const uint2 pagePos = (scrollPos >> 9) & pageShift;
    const uint page = pagePos.x + (pagePos.y << 1);
    const uint pageOffset = page << pageSize;
    const uint pageAddress = pageBaseAddress + pageOffset;

    const uint2 charPatPos = ((scrollPos >> 3) & 0x3F) >> cellSizeShift;
    const uint charIndex = charPatPos.x + (charPatPos.y << (6 - cellSizeShift));
    
    const uint2 cellPos = (scrollPos >> 3) & cellSizeShift;
    uint cellIndex = cellPos.x + (cellPos.y << 1);

    uint2 dotPos = scrollPos & 7;
    
    Character ch;
    if (twoWordChar) {
        ch = FetchTwoWordCharacter(nbgParams, pageAddress, charIndex);
    } else {
        ch = FetchOneWordCharacter(nbgParams, pageAddress, charIndex);
    }
    
    return FetchCharacterPixel(nbgParams, ch, dotPos, cellIndex);
}

uint4 DrawBitmapNBG(uint2 pos, uint index) {
    const RenderState state = renderState[0];
    const uint4 nbgParams = state.nbgParams[index];
  
    if (InsideWindows(nbgParams, pos)) {
        return kTransparentPixel;
    }
    
    const uint2 fracScrollPos = state.nbgScrollAmount[index] + state.nbgScrollInc[index] * pos;
 
    // TODO: mosaic, line screen scroll, vertical cell scroll, data access delays, etc.
    // const bool lineZoomEnable = nbgParams.y & 1;
    // const bool lineScrollXEnable = (nbgParams.y >> 1) & 1;
    // const bool lineScrollYEnable = (nbgParams.y >> 2) & 1;
    // const uint lineScrollInterval = 1 << ((nbgParams.y >> 3) & 3);
    // const uint lineScrollTableAddress = ((nbgParams.y >> 5) & 7) << 17;
    // const bool vertCellScrollEnable = (nbgParams.y >> 8) & 1;
    // const bool vertCellScrollDelay = (nbgParams.y >> 9) & 1;
    // const uint vertCellScrollOffset = ((nbgParams.y >> 10) & 1) << 2;
    // const bool vertCellScrollRepeat = (nbgParams.y >> 11) & 1;
    // const bool mosaicEnable = (nbgParams.x >> 12) & 1;
 
    const uint2 scrollPos = fracScrollPos >> 8;
  
    return FetchBitmapPixel(nbgParams, scrollPos);
}

uint4 DrawNBG(uint2 pos, uint index) {
    const RenderState state = renderState[0];
    const uint4 nbgParams = state.nbgParams[index];
    
    const bool enabled = (nbgParams.x >> 30) & 1;
    if (!enabled) {
        return kTransparentPixel;
    }
    
    const bool bitmap = (nbgParams.x >> 31) & 1;
    return bitmap ? DrawBitmapNBG(pos, index) : DrawScrollNBG(pos, index);
}

uint4 DrawRBG(uint2 pos, uint index) {
    const RenderState state = renderState[0];
    const uint2 rbgParams = state.rbgParams[index];
    
    // TODO: implement
    
    const uint rotIndex = pos.x + GetY(pos.y) * kRotParamLinePitch + index * kRotParamEntryStride;
    return uint4(
        (rotParamState[rotIndex].screenCoords.x >> 0) & 0xFF,
        (rotParamState[rotIndex].screenCoords.x >> 8) & 0xFF,
        (rotParamState[rotIndex].screenCoords.y >> 0) & 0xFF,
        ((rotParamState[rotIndex].screenCoords.y >> 8) & 0x7F) | (rotParamState[rotIndex].coeffData & 0x80)
    );
    //return uint4(rotParamState[rotIndex].screenCoords, rotParamState[rotIndex].spriteCoords, rotParamState[rotIndex].coeffData);
    //return uint4(pos.x, pos.y, index * 255, 128);
}

// The alpha channel of the output is used for pixel attributes as follows:
// bits  use
//  0-3  Priority (0 to 7)
//    6  Special color calculation flag
//    7  Transparent flag (0=opaque, 1=transparent)

[numthreads(32, 1, 6)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint2 drawCoord = id.xy + uint2(0, config.startY);
    const uint3 outCoord = uint3(drawCoord.x, GetY(drawCoord.y), id.z);
    if (id.z < 4) {
        bgOut[outCoord] = DrawNBG(drawCoord, id.z);
    } else {
        bgOut[outCoord] = DrawRBG(drawCoord, id.z - 4);
    }
}
