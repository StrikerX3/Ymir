struct VDP2Regs {
    // Display parameters:
    // bits   use
    //     0  Interlaced
    //     1  Field                    0=even; 1=odd
    //     2  Exclusive monitor mode   0=normal; 1=exclusive
    uint4 displayParams;
    
    // NBG properties:
    // Entry 0 - common properties
    // bits   use
    //   0-3  Character pattern access per bank
    //     4  Character pattern delay
    //     5  Mosaic enable                   0=disable; 1=enable
    //     6  Transparency enable             0=disable; 1=enable
    //     7  Color calculation enable        0=disable; 1=enable
    //  8-10  CRAM offset
    // 11-13  Color format                    0=pal16; 1=pal256; 2=pal2048; 3=rgb555; 4=rgb888; other values not used
    // 14-15  Special color calculation mode  0=per screen; 1=per character; 2=per dot; 3=color data MSB
    //    16  Special function select         0=A; 1=B
    // 17-19  Priority number
    // 20-21  Priority mode                   0=per screen; 1=per character; 2=per dot; 3=invalid/unused
    // 22-24  Supplementary palette number
    //    25  Supplementary special color calculation bit
    //    26  Supplementary special priority bit
    // 27-29  -
    //    30  Background enabled              0=disable; 1=enable
    //    31  Background type (= 0)           0=scroll; 1=bitmap
    //
    // Entry 1 - type-specific parameters -- scroll BGs
    // bits   use
    //   0-3  Pattern name access per bank
    //     4* Horizontal page size shift
    //     5* Vertical page size shift
    //     6* Extended character number     0=10 bits; 1=12 bits, no H/V flip
    //     7* Two-word character            0=one-word (16-bit); 1=two-word (32-bit)
    //     8* Character cell size           0=1x1 cell; 1=2x2 cells
    //     9* Vertical cell scroll enable   0=disable; 1=enable  (NBG0 and NBG1 only)
    // 10-14* Supplementary character number
    // 15-31  -
    //
    // Entry 1 - type-specific parameters -- bitmap BGs
    // bits   use
    //     0  Horizontal bitmap size shift (512 << x)
    //     1  Vertical bitmap size shift (256 << x)
    //  2-31  -
    uint nbgParams[4][2];
    
    // TODO: NBG scroll amounts (H/V)
    // TODO: NBG scroll increments (H/V)
    // TODO: NBG bitmap base address
    // TODO: NBG VRAM data offsets (4 banks)
    // TODO: NBG line scroll enable + offset tables (X/Y)
    // TODO: Special function codes table (for per-dot special color calculations)
    
    uint nbgPageBaseAddresses[4][4]; // [NBG0-3][plane A-D]
    uint rbgPageBaseAddresses[2][16]; // [RBG0-1][plane A-P]
};

ByteAddressBuffer vram : register(t0);
StructuredBuffer<uint> cram : register(t1);
StructuredBuffer<VDP2Regs> vdp2regs : register(t2);
RWTexture2DArray<uint4> textureOut : register(u0);

// The alpha channel of the output is used for pixel attributes as follows:
// bits  use
//  0-3  Priority (0 to 7)
//    6  Special color calculation flag
//    7  Transparent flag (0=opaque, 1=transparent)

static const uint kColorFormatPalette16 = 0;
static const uint kColorFormatPalette256 = 1;
static const uint kColorFormatPalette2048 = 2;
static const uint kColorFormatRGB555 = 3;
static const uint kColorFormatRGB888 = 4;

static const uint kPriorityModeScreen = 0;
static const uint kPriorityModeCharacter = 1;
static const uint kPriorityModeDot = 2;

static const uint kPageSizes[2][2] = { { 13, 14 }, { 11, 12 } };

struct Character {
    uint charNum;
    uint palNum;
    bool specColorCalc;
    bool specPriority;
    bool flipH;
    bool flipV;
};

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

// Expects address to be 16-bit-aligned
uint ReadVRAM16(uint address) {
    return ByteSwap16(vram.Load(address & ~3) >> ((address & 2) * 8));
}

// Expects address to be 32-bit-aligned
uint ReadVRAM32(uint address) {
    return ByteSwap32(vram.Load(address));
}

uint GetY(uint y) {
    const bool interlaced = vdp2regs[y].displayParams.x & 1;
    const bool odd = (vdp2regs[y].displayParams.x >> 1) & 1;
    const bool exclusiveMonitor = (vdp2regs[y].displayParams.x >> 2) & 1;
    if (interlaced && !exclusiveMonitor) {
        return (y << 1) | (odd /* TODO & !deinterlace */);
    } else {
        return y;
    }
}

static const Character kBlankCharacter = (Character) 0;

Character FetchTwoWordCharacter(uint nbgParams[2], uint pageAddress, uint charIndex) {
    const uint patNameAccess = nbgParams[1] & 0xF;
    const uint charAddress = pageAddress + charIndex * 4;
    const uint charBank = (charAddress >> 17) & 3;
 
    if (((patNameAccess >> charBank) & 1) == 0) {
        return kBlankCharacter;
    }
    
    const uint charData = ReadVRAM32(charAddress);

    Character ch;
    ch.charNum = charData & 0x7FFF;
    ch.palNum = (charData >> 16) & 0x7F;
    ch.specColorCalc = (charData >> 28) & 1;
    ch.specPriority = (charData >> 29) & 1;
    ch.flipH = (charData >> 30) & 1;
    ch.flipV = (charData >> 31) & 1;
    return ch;
}

Character FetchOneWordCharacter(uint nbgParams[2], uint pageAddress, uint charIndex) {
    const uint charAddress = pageAddress + charIndex * 2;
    const uint charBank = (charAddress >> 17) & 3;
    const uint patNameAccess = nbgParams[1] & 0xF;
    if (((patNameAccess >> charBank) & 1) == 0) {
        return kBlankCharacter;
    }
    
    const uint charData = ReadVRAM16(charAddress);

    const uint supplScrollCharNum = (nbgParams[1] >> 10) & 0x1F;
    const uint supplScrollPalNum = (nbgParams[0] >> 22) & 7;
    const bool supplScrollSpecialColorCalc = (nbgParams[0] >> 25) & 1;
    const bool supplScrollSpecialPriority = (nbgParams[0] >> 26) & 1;
    const bool extChar = (nbgParams[1] >> 6) & 1;
    const bool cellSizeShift = (nbgParams[1] >> 8) & 1;
    const uint colorFormat = (nbgParams[0] >> 11) & 7;
    
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
        ch.palNum = ((charData >> 12) & 7) << 4;
    } else {
        ch.palNum = ((charData >> 12) & 0xF) | (supplScrollPalNum << 4);
    }
    ch.specColorCalc = supplScrollSpecialColorCalc;
    ch.specPriority = supplScrollSpecialPriority;
    ch.flipH = !extChar && ((charData >> 10) & 1);
    ch.flipV = !extChar && ((charData >> 11) & 1);
    return ch;
}

uint4 FetchCharacterPixel(uint nbgParams[2], Character ch, uint2 dotPos, uint cellIndex) {
    const bool cellSizeShift = (nbgParams[1] >> 8) & 1;
    const bool enableTransparency = (nbgParams[0] >> 6) & 1;
    const uint cramOffset = nbgParams[0] & 0x700;
    const uint colorFormat = (nbgParams[0] >> 11) & 7;
    const uint bgPriorityNum = (nbgParams[0] >> 17) & 7;
    const uint bgPriorityMode = (nbgParams[0] >> 20) & 3;
    const uint charPatAccess = nbgParams[0] & 0xF;

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

    const uint cellAddress = (ch.charNum + cellIndex) << 5;
    const uint dotOffset = dotPos.x + (dotPos.y << 3);
    
    uint dotData;
    uint colorIndex;
    uint colorData;
    if (colorFormat == kColorFormatPalette16) {
        const uint dotAddress = cellAddress + (dotOffset >> 1);
        const uint dotBank = (dotAddress >> 17) & 3;
        if ((charPatAccess >> dotBank) & 1) {
            dotData = ReadVRAM4(dotAddress, ~dotPos.x & 1);
        } else {
            dotData = 0;
        }
        colorIndex = (ch.palNum << 4) | dotData;
        colorData = (dotData >> 1) & 7;
    } else if (colorFormat == kColorFormatPalette256) {
        const uint dotAddress = cellAddress + dotOffset;
        const uint dotBank = (dotAddress >> 17) & 3;
        if ((charPatAccess >> dotBank) & 1) {
            dotData = ReadVRAM8(dotAddress);
        } else {
            dotData = 0;
        }
        colorIndex = ((ch.palNum & 0x70) << 4) | dotData;
        colorData = (dotData >> 1) & 7;
    }
    // TODO: handle kColorFormatPalette2048
    // TODO: handle kColorFormatRGB555
    // TODO: handle kColorFormatRGB888
    else {
        colorIndex = 0;
        colorData = 0;
        dotData = 0;
    }
    
    uint priority = bgPriorityNum;
    if (bgPriorityMode == kPriorityModeCharacter) {
        priority &= ~1;
        priority |= ch.specPriority;
    } else if (bgPriorityMode == kPriorityModeDot) {
        priority &= ~1;
        if (colorFormat <= 2) {
            if (ch.specPriority) {
                // TODO: priority |= specFuncCode.colorMatches[colorData];
            }
        }
    }
    
    // TODO: CRAM mode, affects cramAddress mask
    // mode 0: mask=0x3FF
    // mode 1: mask=0x7FF
    // mode 2: mask=0x3FF
    // mode 3: mask=0x3FF
    const uint cramAddress = (cramOffset + colorIndex) & 0x7FF;
    const uint cramValue = cram[cramAddress];
    const uint3 color = uint3(cramValue & 0xFF, (cramValue >> 8) & 0xFF, (cramValue >> 16) & 0xFF);
    const bool transparent = enableTransparency && dotData == 0;
    const bool specialColorCalc = false; // TODO: getSpecialColorCalcFlag
    return uint4(color, (transparent << 7) | (specialColorCalc << 6) | priority);
}

uint4 DrawScrollNBG(uint2 pos, uint index) {
    const VDP2Regs regs = vdp2regs[pos.y];
    const uint nbgParams[2] = regs.nbgParams[index];
    
    const uint2 pageShift = uint2((nbgParams[1] >> 4) & 1, (nbgParams[1] >> 5) & 1);
    const bool twoWordChar = (nbgParams[1] >> 7) & 1;
    const bool cellSizeShift = (nbgParams[1] >> 8) & 1;
    const bool vertCellScrollEnable = (nbgParams[1] >> 9) & 1;
    const bool mosaicEnable = (nbgParams[0] >> 5) & 1;
    const uint pageSize = kPageSizes[cellSizeShift][twoWordChar];
    
    const uint2 scrollPos = uint2(pos.x, GetY(pos.y)); // TODO: apply scroll values, mosaic, line screen/vertical cell scroll, etc.
  
    const uint2 planePos = (scrollPos >> (pageShift + 9)) & 1;
    const uint plane = planePos.x | (planePos.y << 1);
    const uint pageBaseAddress = regs.nbgPageBaseAddresses[index][plane];
    
    // TODO: apply data access shift hack here
    
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
    // TODO: implement
    return uint4(0, 0, 0, 128);
}

uint4 DrawNBG(uint2 pos, uint index) {
    const VDP2Regs regs = vdp2regs[pos.y];
    const uint nbgParams0 = regs.nbgParams[index][0];
    const bool bitmap = (nbgParams0 >> 31) & 1;
    
    return bitmap ? DrawBitmapNBG(pos, index) : DrawScrollNBG(pos, index);
}

uint4 DrawRBG(uint2 pos, uint index) {
    return uint4(index * 255, pos.x, pos.y, 255);
}

[numthreads(32, 16, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.z < 4) {
        textureOut[uint3(id.x, GetY(id.y), id.z)] = DrawNBG(id.xy, id.z);
    } else {
        textureOut[id] = DrawRBG(id.xy, id.z - 4);
    }
}
