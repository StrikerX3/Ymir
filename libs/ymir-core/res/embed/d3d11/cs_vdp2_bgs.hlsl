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
    uint4 rbgParams[2];
    
    uint2 nbgScrollAmount[4];
    uint2 nbgScrollInc[4];
    
    uint nbgPageBaseAddresses[4][4];
    uint rbgPageBaseAddresses[2][2][16];

    // TODO: NBG line scroll offset tables (X/Y) (or addresses to read from VRAM)
    // TODO: Vertical cell scroll table base address
    // TODO: Mosaic sizes (X/Y)
    // TODO: Sprite layer parameters

    Window windows[2];

    uint commonRotParams;
    
    uint specialFunctionCodes;
    uint2 _padding;
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
Buffer<uint4> cram : register(t1);
StructuredBuffer<RenderState> renderState : register(t2);
Buffer<uint2> rotParams : register(t3);
StructuredBuffer<RotParamState> rotParamState : register(t4);

// The alpha channel of the output is used for pixel attributes as follows:
// bits  use
//  0-3  Priority (0 to 7)
//    6  Special color calculation flag
//    7  Transparent flag (0=opaque, 1=transparent)
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

static const uint kRotParamA = 0;
static const uint kRotParamB = 1;

static const uint kRotParamModeA = 0;
static const uint kRotParamModeB = 1;
static const uint kRotParamModeCoeff = 2;
static const uint kRotParamModeWindow = 3;

static const uint kScreenOverProcessRepeat = 0;
static const uint kScreenOverProcessRepeatChar = 1;
static const uint kScreenOverProcessTransparent = 2;
static const uint kScreenOverProcessFixed512 = 3;

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
    const uint odd = (config.displayParams >> 1) & 1;
    const bool exclusiveMonitor = (config.displayParams >> 2) & 1;
    if (interlaced && !exclusiveMonitor) {
        return (y << 1) | (odd /* TODO & !deinterlace */);
    } else {
        return y;
    }
}

uint4 FetchCRAMColor(uint cramOffset, uint colorIndex) {
    const uint cramAddress = (cramOffset + colorIndex) & kCRAMAddressMask;
    return cram[cramAddress];
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
//   5  Sprite window enable (if hasSpriteWindow==true)
//   6  Sprite window invert (if hasSpriteWindow==true)
bool InsideWindows(uint windowParams, bool hasSpriteWindow, uint2 pos) {
    const Window windows[2] = renderState[0].windows;
    const uint windowLogic = windowParams & 1;
    const bool window0Enable = (windowParams >> 1) & 1;
    const bool window0Invert = (windowParams >> 2) & 1;
    const bool window1Enable = (windowParams >> 3) & 1;
    const bool window1Invert = (windowParams >> 4) & 1;
    const bool spriteWindowEnable = hasSpriteWindow && ((windowParams >> 5) & 1);
    const bool spriteWindowInvert = hasSpriteWindow && ((windowParams >> 6) & 1);
    
    // If no windows are enabled, consider the pixel outside of windows
    if (!window0Enable && !window1Enable && (hasSpriteWindow && !spriteWindowEnable)) {
        return false;
    }
    
    const bool insideW0 = window0Enable && InsideWindow(windows[0], window0Invert, pos);
    const bool insideW1 = window1Enable && InsideWindow(windows[1], window1Invert, pos);
    const bool insideSW = spriteWindowEnable && false; // TODO: InsideSpriteWindow(spriteWindowInvert, pos);
    
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

Character ExtractOneWordCharacter(uint4 bgParams, uint charData) {
    const uint supplScrollCharNum = (bgParams.w >> 9) & 0x1F;
    const uint supplScrollPalNum = ((bgParams.x >> 22) & 7) << 4;
    const bool supplScrollSpecialColorCalc = (bgParams.x >> 25) & 1;
    const bool supplScrollSpecialPriority = (bgParams.x >> 26) & 1;
    const uint extChar = (bgParams.w >> 6) & 1;
    const uint cellSizeShift = (bgParams.w >> 8) & 1;
    const uint colorFormat = (bgParams.x >> 11) & 7;

    // Character number bit range from the 1-word character pattern data (charData)
    const uint baseCharNumMask = extChar ? 0xFFF : 0x3FF;
    const uint baseCharNumPos = 2 * cellSizeShift;

    // Upper character number bit range from the supplementary character number (bgParams.supplCharNum)
    const uint supplCharNumStart = 2 * cellSizeShift + 2 * extChar;
    const uint supplCharNumMask = 0x1F >> supplCharNumStart;
    const uint supplCharNumPos = 10 + supplCharNumStart;
    // The lower bits are always in range 0..1 and only used if cellSizeShift == true

    const uint baseCharNum = charData & baseCharNumMask;
    const uint supplCharNum = (supplScrollCharNum >> supplCharNumStart) & supplCharNumMask;

    Character ch;
    ch.charNum = (baseCharNum << baseCharNumPos) | (supplCharNum << supplCharNumPos);
    if (cellSizeShift > 0) {
        ch.charNum |= supplScrollCharNum & 3;
    }
    if (colorFormat != kColorFormatPalette16) {
        ch.palNum = ((charData >> 12) & 7) << 8;
    } else {
        ch.palNum = (((charData >> 12) & 0xF) | supplScrollPalNum) << 4;
    }
    ch.specColorCalc = supplScrollSpecialColorCalc;
    ch.specPriority = supplScrollSpecialPriority;
    ch.flipH = !extChar && ((charData >> 10) & 1);
    ch.flipV = !extChar && ((charData >> 11) & 1);
    return ch;
}

Character FetchTwoWordCharacter(uint4 bgParams, uint pageAddress, uint charIndex) {
    const uint patNameAccess = bgParams.w & 0xF;
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

Character FetchOneWordCharacter(uint4 bgParams, uint pageAddress, uint charIndex) {
    const uint charAddress = pageAddress + charIndex * 2;
    const uint charBank = (charAddress >> 17) & 3;
    const uint patNameAccess = bgParams.w & 0xF;
    if (((patNameAccess >> charBank) & 1) == 0) {
        return kBlankCharacter;
    }
    
    const uint charData = ReadVRAM16(charAddress);
    return ExtractOneWordCharacter(bgParams, charData);
}

uint4 FetchPixel(uint4 bgParams, uint baseAddress, uint2 dotPos, uint linePitch, uint palNum, bool specColorCalc, uint specPriority) {
    const uint charPatAccess = bgParams.x & 0xF;
    const uint colorFormat = (bgParams.x >> 11) & 7;
    const uint cramOffset = bgParams.x & 0x700;
    const uint bgPriorityNum = (bgParams.x >> 17) & 7;
    const uint bgPriorityMode = (bgParams.x >> 20) & 3;
    const bool enableTransparency = (bgParams.x >> 28) & 1;
  
    const uint dotOffset = dotPos.x + dotPos.y * linePitch;
    
    // TODO: apply VRAM data offset
    
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
        outSpecColorCalc = GetSpecialColorCalcFlag(bgParams, colorData, specColorCalc, outColor.a);

    } else if (colorFormat == kColorFormatPalette256) {
        const uint dotAddress = baseAddress + dotOffset;
        const uint dotBank = (dotAddress >> 17) & 3;
        const uint dotData = ((charPatAccess >> dotBank) & 1) ? ReadVRAM8(dotAddress) : 0;
        const uint colorIndex = (palNum & 0x700) | dotData;
        colorData = (dotData >> 1) & 7;
        outColor = FetchCRAMColor(cramOffset, colorIndex);
        outTransparent = enableTransparency && dotData == 0;
        outSpecColorCalc = GetSpecialColorCalcFlag(bgParams, colorData, specColorCalc, outColor.a);

    } else if (colorFormat == kColorFormatPalette2048) {
        const uint dotAddress = baseAddress + (dotOffset << 1);
        const uint dotBank = (dotAddress >> 17) & 3;
        const uint dotData = ((charPatAccess >> dotBank) & 1) ? ReadVRAM16(dotAddress) : 0;
        const uint colorIndex = dotData & 0x7FF;
        colorData = (dotData >> 1) & 7;
        outColor = FetchCRAMColor(cramOffset, colorIndex);
        outTransparent = enableTransparency && (dotData & 0x7FF) == 0;
        outSpecColorCalc = GetSpecialColorCalcFlag(bgParams, colorData, specColorCalc, outColor.a);

    } else if (colorFormat == kColorFormatRGB555) {
        const uint dotAddress = baseAddress + (dotOffset << 1);
        const uint dotBank = (dotAddress >> 17) & 3;
        const uint dotData = ((charPatAccess >> dotBank) & 1) ? ReadVRAM16(dotAddress) : 0;
        outColor = Color555(dotData);
        outTransparent = enableTransparency && outColor.w == 0;
        outSpecColorCalc = GetSpecialColorCalcFlag(bgParams, 7, specColorCalc, true);

    } else if (colorFormat == kColorFormatRGB888) {
        const uint dotAddress = baseAddress + (dotOffset << 2);
        const uint dotBank = (dotAddress >> 17) & 3;
        const uint dotData = ((charPatAccess >> dotBank) & 1) ? ReadVRAM32(dotAddress) : 0;
        outColor = Color888(dotData);
        outTransparent = enableTransparency && outColor.w == 0;
        outSpecColorCalc = GetSpecialColorCalcFlag(bgParams, 7, specColorCalc, true);

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
            outPriority |= IsSpecialColorCalcMatch(bgParams, colorData);
        }
    }
    
    return uint4(outColor.xyz, (outTransparent << 7) | (outSpecColorCalc << 6) | outPriority);
}

uint4 FetchCharacterPixel(uint4 bgParams, Character ch, uint2 dotPos, uint cellIndex) {
    const uint cellSizeShift = (bgParams.w >> 8) & 1;
    const uint colorFormat = (bgParams.x >> 11) & 7;

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
    return FetchPixel(bgParams, baseAddress, dotPos, 8, ch.palNum, ch.specColorCalc, ch.specPriority);
}

uint4 FetchBitmapPixel(uint4 bgParams, uint2 scrollPos) {
    const uint bitmapSizeH = 512 << (bgParams.w & 1);
    const uint bitmapSizeV = 256 << ((bgParams.w >> 1) & 1);

    const uint2 dotPos = scrollPos & uint2(bitmapSizeH - 1, bitmapSizeV - 1);
    const uint baseAddress = ((bgParams.w >> 2) & 7) << 17;
    const uint palNum = ((bgParams.x >> 22) & 7) << 8;
    const bool specColorCalc = (bgParams.x >> 25) & 1;
    const uint specPriority = (bgParams.x >> 26) & 1;
 
    return FetchPixel(bgParams, baseAddress, dotPos, bitmapSizeH, palNum, specColorCalc, specPriority);
}

uint4 FetchScrollBGPixel(uint4 bgParams, uint2 scrollPos, uint2 pageShift, bool rot, uint pageBaseAddresses[16]) {
    const uint planeShift = rot ? 2 : 1;
    const uint planeMask = (1 << planeShift) - 1;
    
    const uint twoWordChar = (bgParams.w >> 7) & 1;
    const uint cellSizeShift = (bgParams.w >> 8) & 1;
    const uint pageSize = kPageSizes[cellSizeShift][twoWordChar];

    const uint2 planePos = (scrollPos >> (pageShift + 9)) & planeMask;
    const uint plane = planePos.x | (planePos.y << planeShift);
    const uint pageBaseAddress = pageBaseAddresses[plane];
    
    // HACK: apply data access shift
    // Not entirely correct, but fixes problems with World Heroes Perfect's demo screen
    const uint bank = (pageBaseAddress >> 17) & 3;
    if ((bgParams.x >> (4 + bank)) & 1) {
        scrollPos.x += 8;
    }
    
    const uint2 pagePos = (scrollPos >> 9) & pageShift;
    const uint page = pagePos.x + (pagePos.y << 1);
    const uint pageOffset = page << pageSize;
    const uint pageAddress = pageBaseAddress + pageOffset;

    // HACK: work around FXC bug that produces invalid code for the line below:
    //   const uint2 charPatPos = ((scrollPos >> 3) & 0x3F) >> cellSizeShift;
    // When cellSizeShift is derived from a masked/shifted value, the compiler merges the two shifts into one:
    //   const uint2 charPatPos = (scrollPos >> (3 + cellSizeShift) & 0x3F;
    // See https://shader-playground.timjones.io/1df21a52a4e485bd355e1c9bab45bbd8
    const uint2 baseCharPatPos = (scrollPos >> 3) & 0x3F;
    const uint2 charPatPos = cellSizeShift ? (baseCharPatPos >> 1) : baseCharPatPos;
    const uint charIndex = charPatPos.x + (charPatPos.y << (6 - cellSizeShift));
    
    const uint2 cellPos = (scrollPos >> 3) & cellSizeShift;
    uint cellIndex = cellPos.x + (cellPos.y << 1);

    uint2 dotPos = scrollPos & 7;
    
    Character ch;
    if (twoWordChar) {
        ch = FetchTwoWordCharacter(bgParams, pageAddress, charIndex);
    } else {
        ch = FetchOneWordCharacter(bgParams, pageAddress, charIndex);
    }
    return FetchCharacterPixel(bgParams, ch, dotPos, cellIndex);
}

uint4 FetchScrollNBGPixel(uint4 bgParams, uint2 scrollPos, uint pageBaseAddresses[4]) {
    const uint2 pageShift = uint2((bgParams.w >> 4) & 1, (bgParams.w >> 5) & 1);
 
    uint pbaResized[16];
    for (int i = 0; i < 4; i++) {
        pbaResized[i] = pageBaseAddresses[i];
    }
    
    return FetchScrollBGPixel(bgParams, scrollPos, pageShift, false, pbaResized);
}

uint4 FetchScrollRBGPixel(uint4 bgParams, uint2 scrollPos, uint2 pageShift, uint pageBaseAddresses[16]) {
    return FetchScrollBGPixel(bgParams, scrollPos, pageShift, true, pageBaseAddresses);
}

// -----------------------------------------------------------------------------

uint4 DrawScrollNBG(uint2 pos, uint index) {
    const RenderState state = renderState[0];
    const uint4 nbgParams = state.nbgParams[index];
  
    const uint2 pageShift = uint2((nbgParams.w >> 4) & 1, (nbgParams.w >> 5) & 1);
    const uint twoWordChar = (nbgParams.w >> 7) & 1;
    const uint cellSizeShift = (nbgParams.w >> 8) & 1;
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
  
    return FetchScrollNBGPixel(nbgParams, scrollPos, state.nbgPageBaseAddresses[index]);
}

uint4 DrawBitmapNBG(uint2 pos, uint index) {
    const RenderState state = renderState[0];
    const uint4 nbgParams = state.nbgParams[index];
    
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
    
    if (InsideWindows(nbgParams.y >> 13, true, pos)) {
        return kTransparentPixel;
    }
  
    const bool bitmap = (nbgParams.x >> 31) & 1;
    return bitmap ? DrawBitmapNBG(pos, index) : DrawScrollNBG(pos, index);
}

// -----------------------------------------------------------------------------

uint GetRotIndex(uint2 pos, uint paramIndex) {
    return pos.x + pos.y * kRotParamLinePitch + paramIndex * kRotParamEntryStride;
}

uint SelectRotationParameter(uint4 rbgParams, uint2 pos) {
    const uint commonRotParams = renderState[0].commonRotParams;
    const uint rotParamMode = commonRotParams & 3;
    switch (rotParamMode) {
        case kRotParamModeA:
            return kRotParamA;
        case kRotParamModeB:
            return kRotParamB;
        case kRotParamModeCoeff:{
                const bool coeffTableEnable = (rotParams[0].x >> 0) & 1;
                if (!coeffTableEnable) {
                    return kRotParamA;
                }
                const uint rotIndex = GetRotIndex(pos, 0);
                const uint coeffData = rotParamState[rotIndex].coeffData;
                const bool transparent = (coeffData >> 7) & 1;
                return transparent ? kRotParamB : kRotParamA;
            }
        case kRotParamModeWindow:
            return InsideWindows(commonRotParams >> 2, false, pos) ? kRotParamB : kRotParamA;
    }
    return kRotParamA; // shouldn't happen
}

uint4 DrawScrollRBG(uint2 pos, uint index, uint rotSel) {
    const RenderState state = renderState[0];
    const uint4 rbgParams = state.rbgParams[index];
    const uint screenOverProcess = (rbgParams.z >> 16) & 3;
    const uint screenOverPatternName = rbgParams.z & 0xFFFF;

    const uint4 rotParams = state.rbgParams[rotSel];
    const uint2 pageShift = uint2((rotParams.w >> 4) & 1, (rotParams.w >> 5) & 1);

    const uint rotIndex = GetRotIndex(pos, rotSel);
    const RotParamState rotState = rotParamState[rotIndex];

    // Determine maximum coordinates and screen over process
    const bool usingFixed512 = screenOverProcess == kScreenOverProcessFixed512;
    const bool usingRepeat = screenOverProcess == kScreenOverProcessRepeat;
    const uint2 scrollSize = usingFixed512
        ? uint2(512, 512)
        : uint2(512 * 4, 512 * 4) << pageShift;
    
    const uint2 scrollPos = rotState.screenCoords;
    if (all(scrollPos < scrollSize) || usingRepeat) {
        // Plot pixel
        
        // TODO: VDP2StoreRotationLineColorData<bgIndex>(x, bgParams, rotParamSelector);
    
        return FetchScrollRBGPixel(rbgParams, scrollPos, pageShift, state.rbgPageBaseAddresses[rotSel][index]);
    }

    // Out of bounds
    
    if (screenOverProcess == kScreenOverProcessRepeatChar) {
        // TODO: VDP2StoreRotationLineColorData<bgIndex>(x, bgParams, rotParamSelector);
       
        const uint2 dotPos = scrollPos & 7;
        Character ch = ExtractOneWordCharacter(rbgParams, screenOverPatternName);
        return FetchCharacterPixel(rbgParams, ch, dotPos, 0);
    }
    return kTransparentPixel;
}

uint4 DrawBitmapRBG(uint2 pos, uint index, uint rotSel) {
    const RenderState state = renderState[0];
    const uint4 rbgParams = state.rbgParams[index];
    const uint rotIndex = GetRotIndex(pos, rotSel);
    const RotParamState rotState = rotParamState[rotIndex];
  
    // TODO: implement
    
    return uint4(
        (rotState.screenCoords.x >> 0) & 0xFF,
        ((rotState.screenCoords.x >> 8) & 0x7F) | 0x80, // bit 7 = scroll/bitmap (=1)
        (rotState.screenCoords.y >> 0) & 0xFF,
        ((rotState.screenCoords.y >> 8) & 0x7F) | (rotState.coeffData & 0x80)
    );
    //return uint4(rotParamState[rotIndex].screenCoords, rotParamState[rotIndex].spriteCoords, rotParamState[rotIndex].coeffData);
    //return uint4(pos.x, pos.y, index * 255, 128);
}

uint4 DrawRBG(uint2 pos, uint index) {
    const bool hiResH = (config.displayParams >> 5) & 1;
    if (hiResH) {
        pos.x >>= 1;
    }

    const RenderState state = renderState[0];
    const uint4 rbgParams = state.rbgParams[index];
        
    const bool enabled = (rbgParams.x >> 30) & 1;
    if (!enabled) {
        return kTransparentPixel;
    }
    
    if (InsideWindows(rbgParams.y >> 13, true, pos)) {
        return kTransparentPixel;
    }

    const uint rotSel = index == 0 ? SelectRotationParameter(rbgParams, pos) : kRotParamB;
    const uint rotIndex = GetRotIndex(pos, rotSel);
    const RotParamState rotState = rotParamState[rotIndex];

    // Handle transparent pixels in coefficient table
    const bool coeffTableEnable = (rotParams[0].x >> 0) & 1;
    if (coeffTableEnable && ((rotState.coeffData >> 7) & 1)) {
        return kTransparentPixel;
    }

    const bool bitmap = (rbgParams.x >> 31) & 1;
    return bitmap ? DrawBitmapRBG(pos, index, rotSel) : DrawScrollRBG(pos, index, rotSel);
}

// -----------------------------------------------------------------------------

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
