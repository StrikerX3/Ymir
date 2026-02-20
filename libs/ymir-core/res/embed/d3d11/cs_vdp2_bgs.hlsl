struct Config {
    uint displayParams;
    uint startY;
    uint layerEnabled;
    uint _reserved;
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
    
    uint lineScreenParams;
    uint backScreenParams;
    
    uint specialFunctionCodes;
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

// The alpha channel of the BG output is used for pixel attributes as follows:
// bits  use
//  0-2  Priority (0 to 7)
//    6  Special color calculation flag
//    7  Transparent flag (0=opaque, 1=transparent)
RWTexture2DArray<uint4> bgOut : register(u0);
RWTexture2DArray<uint4> rbgLineColorOut : register(u1);
RWTexture2D<uint4> lineColorOut : register(u2);

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

uint ByteSwap32(uint val) {
    return ((val >> 24) & 0x000000FF) |
           ((val >> 8) & 0x0000FF00) |
           ((val << 8) & 0x00FF0000) |
           ((val << 24) & 0xFF000000);
}

uint ReadVRAM4(uint address, uint nibble) {
    return BitExtract(vram.Load(address & ~3), (address & 3) * 8 + nibble * 4, 4);
}

uint ReadVRAM8(uint address) {
    return BitExtract(vram.Load(address & ~3), (address & 3) * 8, 8);
}

uint ReadVRAM16(uint address) {
    return ByteSwap16(BitExtract(vram.Load(address & ~3), (address & 2) * 8, 16));
}

// Expects address to be 32-bit-aligned
uint ReadVRAM32(uint address) {
    return ByteSwap32(vram.Load(address));
}

uint GetY(uint y) {
    const bool interlaced = BitTest(config.displayParams, 0);
    const uint odd = BitExtract(config.displayParams, 1, 1);
    const bool exclusiveMonitor = BitTest(config.displayParams, 2);
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
    const bool hiResH = BitTest(config.displayParams, 5);
    
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
    const uint windowLogic = BitExtract(windowParams, 0, 1);
    const bool window0Enable = BitTest(windowParams, 1);
    const bool window0Invert = BitTest(windowParams, 2);
    const bool window1Enable = BitTest(windowParams, 3);
    const bool window1Invert = BitTest(windowParams, 4);
    const bool spriteWindowEnable = hasSpriteWindow && BitTest(windowParams, 5);
    const bool spriteWindowInvert = hasSpriteWindow && BitTest(windowParams, 6);
    
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
    const uint specFuncSelect = BitExtract(nbgParams.x, 16, 1);
    return BitTest(renderState[0].specialFunctionCodes, specFuncSelect * 8 + specColorCode);
}

bool GetSpecialColorCalcFlag(uint4 nbgParams, uint specColorCode, bool specColorCalc, bool colorMSB) {
    const bool colorCalcEnable = BitTest(nbgParams.x, 29);
    if (!colorCalcEnable) {
        return false;
    }
    
    const uint specColorCalcMode = BitExtract(nbgParams.x, 14, 2);
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
    return false;
}

Character ExtractOneWordCharacter(uint4 bgParams, uint charData) {
    const uint supplScrollCharNum = BitExtract(bgParams.w, 9, 5);
    const uint supplScrollPalNum = BitExtract(bgParams.x, 22, 3) << 4;
    const bool supplScrollSpecialColorCalc = BitTest(bgParams.x, 25);
    const bool supplScrollSpecialPriority = BitTest(bgParams.x, 26);
    const uint extChar = BitExtract(bgParams.w, 6, 1);
    const uint cellSizeShift = BitExtract(bgParams.w, 8, 1);
    const uint colorFormat = BitExtract(bgParams.x, 11, 3);

    // Character number bit range from the 1-word character pattern data (charData)
    const uint baseCharNumMask = extChar != 0 ? 0xFFF : 0x3FF;
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
        ch.charNum |= BitExtract(supplScrollCharNum, 0, 2);
    }
    if (colorFormat != kColorFormatPalette16) {
        ch.palNum = BitExtract(charData, 12, 3) << 8;
    } else {
        ch.palNum = (BitExtract(charData, 12, 4) | supplScrollPalNum) << 4;
    }
    ch.specColorCalc = supplScrollSpecialColorCalc;
    ch.specPriority = supplScrollSpecialPriority;
    ch.flipH = extChar == 0 && BitTest(charData, 10);
    ch.flipV = extChar == 0 && BitTest(charData, 11);
    return ch;
}

Character FetchTwoWordCharacter(uint4 bgParams, uint pageAddress, uint charIndex) {
    const uint patNameAccess = BitExtract(bgParams.w, 0, 4);
    const uint charAddress = pageAddress + charIndex * 4;
    const uint charBank = BitExtract(charAddress, 17, 2);
 
    if (!BitTest(patNameAccess, charBank)) {
        return kBlankCharacter;
    }
    
    const uint charData = ReadVRAM32(charAddress);

    Character ch;
    ch.charNum = BitExtract(charData, 0, 15);
    ch.palNum = BitExtract(charData, 16, 7) << 4;
    ch.specColorCalc = BitTest(charData, 28);
    ch.specPriority = BitTest(charData, 29);
    ch.flipH = BitTest(charData, 30);
    ch.flipV = BitTest(charData, 31);
    return ch;
}

Character FetchOneWordCharacter(uint4 bgParams, uint pageAddress, uint charIndex) {
    const uint charAddress = pageAddress + charIndex * 2;
    const uint charBank = BitExtract(charAddress, 17, 2);
    const uint patNameAccess = BitExtract(bgParams.w, 0, 4);
    if (!BitTest(patNameAccess, charBank)) {
        return kBlankCharacter;
    }
    
    const uint charData = ReadVRAM16(charAddress);
    return ExtractOneWordCharacter(bgParams, charData);
}

uint4 FetchPixel(uint4 bgParams, uint baseAddress, uint2 dotPos, uint linePitch, uint palNum, bool specColorCalc, uint specPriority) {
    const uint charPatAccess = BitExtract(bgParams.x, 0, 4);
    const uint colorFormat = BitExtract(bgParams.x, 11, 3);
    const uint cramOffset = bgParams.x & 0x700;
    const uint bgPriorityNum = BitExtract(bgParams.x, 17, 3);
    const uint bgPriorityMode = BitExtract(bgParams.x, 20, 2);
    const bool enableTransparency = BitTest(bgParams.x, 28);
  
    const uint dotOffset = dotPos.x + dotPos.y * linePitch;
    
    // TODO: apply VRAM data offset
    
    uint colorData;
    uint4 outColor;
    bool outTransparent;
    bool outSpecColorCalc;
    if (colorFormat == kColorFormatPalette16) {
        const uint dotAddress = baseAddress + (dotOffset >> 1);
        const uint dotBank = BitExtract(dotAddress, 17, 2);
        const uint dotData = BitTest(charPatAccess, dotBank) ? ReadVRAM4(dotAddress, ~dotPos.x & 1) : 0;
        const uint colorIndex = palNum | dotData;
        colorData = BitExtract(dotData, 1, 3);
        outColor = FetchCRAMColor(cramOffset, colorIndex);
        outTransparent = enableTransparency && dotData == 0;
        outSpecColorCalc = GetSpecialColorCalcFlag(bgParams, colorData, specColorCalc, BitTest(outColor.a, 0));

    } else if (colorFormat == kColorFormatPalette256) {
        const uint dotAddress = baseAddress + dotOffset;
        const uint dotBank = BitExtract(dotAddress, 17, 2);
        const uint dotData = BitTest(charPatAccess, dotBank) ? ReadVRAM8(dotAddress) : 0;
        const uint colorIndex = (palNum & 0x700) | dotData;
        colorData = BitExtract(dotData, 1, 3);
        outColor = FetchCRAMColor(cramOffset, colorIndex);
        outTransparent = enableTransparency && dotData == 0;
        outSpecColorCalc = GetSpecialColorCalcFlag(bgParams, colorData, specColorCalc, BitTest(outColor.a, 0));

    } else if (colorFormat == kColorFormatPalette2048) {
        const uint dotAddress = baseAddress + (dotOffset << 1);
        const uint dotBank = BitExtract(dotAddress, 17, 2);
        const uint dotData = BitTest(charPatAccess, dotBank) ? ReadVRAM16(dotAddress) : 0;
        const uint colorIndex = dotData & 0x7FF;
        colorData = BitExtract(dotData, 1, 3);
        outColor = FetchCRAMColor(cramOffset, colorIndex);
        outTransparent = enableTransparency && (dotData & 0x7FF) == 0;
        outSpecColorCalc = GetSpecialColorCalcFlag(bgParams, colorData, specColorCalc, BitTest(outColor.a, 0));

    } else if (colorFormat == kColorFormatRGB555) {
        const uint dotAddress = baseAddress + (dotOffset << 1);
        const uint dotBank = BitExtract(dotAddress, 17, 2);
        const uint dotData = BitTest(charPatAccess, dotBank) ? ReadVRAM16(dotAddress) : 0;
        outColor = Color555(dotData);
        outTransparent = enableTransparency && outColor.w == 0;
        outSpecColorCalc = GetSpecialColorCalcFlag(bgParams, 7, specColorCalc, true);

    } else if (colorFormat == kColorFormatRGB888) {
        const uint dotAddress = baseAddress + (dotOffset << 2);
        const uint dotBank = BitExtract(dotAddress, 17, 2);
        const uint dotData = BitTest(charPatAccess, dotBank) ? ReadVRAM32(dotAddress) : 0;
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
        if (specPriority != 0 && colorFormat < kColorFormatRGB555) {
            outPriority |= IsSpecialColorCalcMatch(bgParams, colorData);
        }
    }
    
    return uint4(outColor.xyz, (outTransparent << 7) | (outSpecColorCalc << 6) | outPriority);
}

uint4 FetchCharacterPixel(uint4 bgParams, Character ch, uint2 dotPos, uint cellIndex) {
    const uint cellSizeShift = BitExtract(bgParams.w, 8, 1);
    const uint colorFormat = BitExtract(bgParams.x, 11, 3);

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
    const uint bitmapSizeH = 512 << BitExtract(bgParams.w, 0, 1);
    const uint bitmapSizeV = 256 << BitExtract(bgParams.w, 1, 1);

    const uint2 dotPos = scrollPos & uint2(bitmapSizeH - 1, bitmapSizeV - 1);
    const uint baseAddress = BitExtract(bgParams.w, 2, 3) << 17;
    const uint palNum = BitExtract(bgParams.x, 22, 3) << 8;
    const bool specColorCalc = BitTest(bgParams.x, 25);
    const uint specPriority = BitExtract(bgParams.x, 26, 1);
 
    return FetchPixel(bgParams, baseAddress, dotPos, bitmapSizeH, palNum, specColorCalc, specPriority);
}

uint4 FetchScrollBGPixel(uint4 bgParams, uint2 scrollPos, uint2 pageShift, bool rot, uint pageBaseAddresses[16]) {
    const uint planeShift = rot ? 2 : 1;
    const uint planeMask = (1 << planeShift) - 1;
    
    const uint twoWordChar = BitExtract(bgParams.w, 7, 1);
    const uint cellSizeShift = BitExtract(bgParams.w, 8, 1);
    const uint pageSize = kPageSizes[cellSizeShift][twoWordChar];

    const uint2 planePos = (scrollPos >> (pageShift + 9)) & planeMask;
    const uint plane = planePos.x | (planePos.y << planeShift);
    const uint pageBaseAddress = pageBaseAddresses[plane];
    
    // HACK: apply data access shift
    // Not entirely correct, but fixes problems with World Heroes Perfect's demo screen
    const uint bank = BitExtract(pageBaseAddress, 17, 2);
    if (BitTest(bgParams.x, 4 + bank)) {
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
    const uint2 charPatPos = cellSizeShift != 0 ? (baseCharPatPos >> 1) : baseCharPatPos;
    const uint charIndex = charPatPos.x + (charPatPos.y << (6 - cellSizeShift));
    
    const uint2 cellPos = (scrollPos >> 3) & cellSizeShift;
    uint cellIndex = cellPos.x + (cellPos.y << 1);

    uint2 dotPos = scrollPos & 7;
    
    Character ch;
    if (twoWordChar != 0) {
        ch = FetchTwoWordCharacter(bgParams, pageAddress, charIndex);
    } else {
        ch = FetchOneWordCharacter(bgParams, pageAddress, charIndex);
    }
    return FetchCharacterPixel(bgParams, ch, dotPos, cellIndex);
}

uint4 FetchScrollNBGPixel(uint4 bgParams, uint2 scrollPos, uint pageBaseAddresses[4]) {
    const uint2 pageShift = uint2(BitExtract(bgParams.w, 4, 1), BitExtract(bgParams.w, 5, 1));
 
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
  
    const uint2 pageShift = uint2(BitExtract(nbgParams.w, 4, 1), BitExtract(nbgParams.w, 5, 1));
    const uint twoWordChar = BitExtract(nbgParams.w, 7, 1);
    const uint cellSizeShift = BitExtract(nbgParams.w, 8, 1);
    const uint pageSize = kPageSizes[cellSizeShift][twoWordChar];
    
    pos.y = GetY(pos.y);
    
    const uint2 fracScrollPos = state.nbgScrollAmount[index] + state.nbgScrollInc[index] * pos;

    // TODO: mosaic, line screen scroll, vertical cell scroll, data access delays, etc.
    // const bool lineZoomEnable = BitTest(nbgParams.y, 0);
    // const bool lineScrollXEnable = BitTest(nbgParams.y, 1);
    // const bool lineScrollYEnable = BitTest(nbgParams.y, 2);
    // const uint lineScrollInterval = 1 << BitExtract(nbgParams.y, 3, 2;
    // const uint lineScrollTableAddress = BitExtract(nbgParams.y, 5, 3) << 17;
    // const bool vertCellScrollEnable = BitTest(nbgParams.y, 8);
    // const bool vertCellScrollDelay = BitTest(nbgParams.y, 9);
    // const uint vertCellScrollOffset = BitExtract(nbgParams.y, 10, 1) << 2;
    // const bool vertCellScrollRepeat = BitTest(nbgParams.y, 11);
    // const bool mosaicEnable = BitTest(nbgParams.x, 12);
    
    const uint2 scrollPos = fracScrollPos >> 8;
  
    return FetchScrollNBGPixel(nbgParams, scrollPos, state.nbgPageBaseAddresses[index]);
}

uint4 DrawBitmapNBG(uint2 pos, uint index) {
    const RenderState state = renderState[0];
    const uint4 nbgParams = state.nbgParams[index];
    
    const uint2 fracScrollPos = state.nbgScrollAmount[index] + state.nbgScrollInc[index] * pos;
 
    // TODO: mosaic, line screen scroll, vertical cell scroll, data access delays, etc.
    // const bool lineZoomEnable = BitTest(nbgParams.y, 0);
    // const bool lineScrollXEnable = BitTest(nbgParams.y, 1);
    // const bool lineScrollYEnable = BitTest(nbgParams.y, 2);
    // const uint lineScrollInterval = 1 << BitExtract(nbgParams.y, 3, 2;
    // const uint lineScrollTableAddress = BitExtract(nbgParams.y, 5, 3) << 17;
    // const bool vertCellScrollEnable = BitTest(nbgParams.y, 8);
    // const bool vertCellScrollDelay = BitTest(nbgParams.y, 9);
    // const uint vertCellScrollOffset = BitExtract(nbgParams.y, 10, 1) << 2;
    // const bool vertCellScrollRepeat = BitTest(nbgParams.y, 11);
    // const bool mosaicEnable = BitTest(nbgParams.x, 12);
 
    const uint2 scrollPos = fracScrollPos >> 8;
  
    return FetchBitmapPixel(nbgParams, scrollPos);
}

uint4 DrawNBG(uint2 pos, uint index) {
    const RenderState state = renderState[0];
    const uint4 nbgParams = state.nbgParams[index];
    
    const bool enabled = BitTest(nbgParams.x, 30);
    if (!enabled) {
        return kTransparentPixel;
    }
    
    if (InsideWindows(nbgParams.y >> 13, true, pos)) {
        return kTransparentPixel;
    }
  
    const bool bitmap = BitTest(nbgParams.x, 31);
    return bitmap ? DrawBitmapNBG(pos, index) : DrawScrollNBG(pos, index);
}

// -----------------------------------------------------------------------------

uint GetRotIndex(uint2 pos, uint paramIndex) {
    return pos.x + pos.y * kRotParamLinePitch + paramIndex * kRotParamEntryStride;
}

uint SelectRotationParameter(uint4 rbgParams, uint2 pos) {
    const uint commonRotParams = renderState[0].commonRotParams;
    const uint rotParamMode = BitExtract(commonRotParams, 0, 2);
    switch (rotParamMode) {
        case kRotParamModeA:
            return kRotParamA;
        case kRotParamModeB:
            return kRotParamB;
        case kRotParamModeCoeff:{
                const bool coeffTableEnable = BitTest(rotParams[0].x, 0);
                if (!coeffTableEnable) {
                    return kRotParamA;
                }
                const uint rotIndex = GetRotIndex(pos, 0);
                const uint coeffData = rotParamState[rotIndex].coeffData;
                const bool transparent = BitTest(coeffData, 7);
                return transparent ? kRotParamB : kRotParamA;
            }
        case kRotParamModeWindow:
            return InsideWindows(commonRotParams >> 2, false, pos) ? kRotParamB : kRotParamA;
    }
    return kRotParamA; // shouldn't happen
}

void StoreRotationLineColorData(uint2 pos, uint index, uint rotSel) {
    const bool lineColorEnabled = BitTest(config.layerEnabled, 6 + index);
    if (!lineColorEnabled) {
        return;
    }
    
    const RenderState state = renderState[0];
    const uint commonRotParams = state.commonRotParams;
    const uint rotParamMode = BitExtract(commonRotParams, 0, 2);
    const bool hasRBG1 = BitTest(config.layerEnabled, 13);

    bool useCoeffLineColor = false;
    uint coeffSel;
    
    switch (rotParamMode) {
        case kRotParamModeA:
            useCoeffLineColor = rotSel == kRotParamA;
            coeffSel = kRotParamA;
            break;
        case kRotParamModeB:
            useCoeffLineColor = rotSel == kRotParamB;
            coeffSel = hasRBG1 ? kRotParamA : kRotParamB;
            break;
        case kRotParamModeCoeff:
            useCoeffLineColor = true;
            coeffSel = kRotParamA;
            break;
        case kRotParamModeWindow:
            useCoeffLineColor = true;
            coeffSel = hasRBG1 ? kRotParamA : rotSel;
            break;
    }

    const bool lineColorPerLine = BitTest(state.lineScreenParams, 19);
    const uint lineColorBaseAddress = BitExtract(state.lineScreenParams, 0, 19);
    
    const uint lineColorY = lineColorPerLine ? pos.y : 0;
    const uint lineColorAddress = lineColorBaseAddress + lineColorY;

    uint cramAddress = ReadVRAM16(lineColorAddress);
    
    if (useCoeffLineColor) {
        const uint2 rotParam = rotParams[index];
        const bool coeffTableEnable = BitTest(rotParam.x, 0);
        const bool coeffUseLineColorData = BitTest(rotParam.x, 10);
        if (coeffTableEnable && coeffUseLineColorData) {
            const uint baseLineColorData = BitExtract(cramAddress, 7, 4);
            
            const uint rotIndex = GetRotIndex(pos.xy, coeffSel);
            const uint lineColorData = BitExtract(rotParamState[rotIndex].coeffData, 0, 7);
            
            cramAddress = (baseLineColorData << 7) | lineColorData;
        }
    }
    
    rbgLineColorOut[uint3(pos.xy, index)] = cram[cramAddress];
}

uint4 DrawScrollRBG(uint2 pos, uint index, uint rotSel) {
    const RenderState state = renderState[0];
    const uint4 rbgParams = state.rbgParams[index];
    const uint screenOverProcess = BitExtract(rbgParams.z, 16, 2);
    const uint screenOverPatternName = BitExtract(rbgParams.z, 0, 16);

    const uint4 rotParams = state.rbgParams[rotSel];
    const uint2 pageShift = uint2(BitExtract(rotParams.w, 4, 1), BitExtract(rotParams.w, 5, 1));

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
        StoreRotationLineColorData(pos, index, rotSel);
        
        return FetchScrollRBGPixel(rbgParams, scrollPos, pageShift, state.rbgPageBaseAddresses[rotSel][index]);
    }

    // Out of bounds
    
    if (screenOverProcess == kScreenOverProcessRepeatChar) {
        StoreRotationLineColorData(pos, index, rotSel);
        
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
    const bool hiResH = BitTest(config.displayParams, 5);
    if (hiResH) {
        pos.x >>= 1;
    }

    const RenderState state = renderState[0];
    const uint4 rbgParams = state.rbgParams[index];
        
    const bool enabled = BitTest(rbgParams.x, 30);
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
    const bool coeffTableEnable = BitTest(rotParams[0].x, 0);
    if (coeffTableEnable && BitTest(rotState.coeffData, 7)) {
        return kTransparentPixel;
    }

    const bool bitmap = BitTest(rbgParams.x, 31);
    return bitmap ? DrawBitmapRBG(pos, index, rotSel) : DrawScrollRBG(pos, index, rotSel);
}

// -----------------------------------------------------------------------------

uint4 DrawLineBackScreen(uint index, uint y) {
    const RenderState state = renderState[0];
    const uint params = index == 0 ? state.lineScreenParams : state.backScreenParams;
    
    const bool lineColorPerLine = BitTest(params, 19);
    const uint lineColorBaseAddress = BitExtract(params, 0, 19);
    
    const uint lineColorY = lineColorPerLine ? y : 0;
    const uint lineColorAddress = lineColorBaseAddress + lineColorY;

    const uint cramAddress = ReadVRAM16(lineColorAddress);
    return cram[cramAddress];
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
    
    if (id.z == 0 && id.x < 2) {
        lineColorOut[outCoord.xy] = DrawLineBackScreen(drawCoord.x, drawCoord.y);
    }
}
