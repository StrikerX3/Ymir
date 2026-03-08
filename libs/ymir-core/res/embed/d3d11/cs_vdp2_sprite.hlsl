struct Config {
    uint displayParams;
    uint startY;
    uint extraParams;
    uint vcellScrollParams;
    uint2 spriteParams;
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

Buffer<uint4> cramColor : register(t0);
StructuredBuffer<RotParamState> rotParamState : register(t1);
ByteAddressBuffer spriteFB : register(t2);

// The alpha channel of the BG output is used for pixel attributes as follows:
// bits  use
//  0-2  Priority (0 to 7)
//    3  Color MSB (sprite only)
//    4  Sprite shadow/window flag (sprite only; SD = 1)
//    5  Sprite normal shadow flag (sprite only; DC LSB = 0, rest of the bits = 1)
//    6  Special color calculation flag
//    7  Transparent flag (0=opaque, 1=transparent)
RWTexture2DArray<uint4> bgOut : register(u0);
RWTexture2D<uint> spriteAttrsOut : register(u1);

// -----------------------------------------------------------------------------

static const uint kPixelAttrBitSpriteColorMSB = 3;
static const uint kPixelAttrBitSpriteShadowWindow = 4;
static const uint kPixelAttrBitSpriteNormalShadow = 5;
static const uint kPixelAttrBitSpecColorCalc = 6;
static const uint kPixelAttrBitTransparent = 7;

static const uint kCRAMAddressMask = ((config.displayParams >> 4) & 3) == 1 ? 0x7FF : 0x3FF;

static const uint4 kTransparentPixel = uint4(0, 0, 0, 1 << kPixelAttrBitTransparent);

// -----------------------------------------------------------------------------

bool BitTest(uint value, uint bit) {
    return ((value >> bit) & 1) != 0;
}

uint BitExtract(uint value, uint offset, uint length) {
    const uint mask = (1u << length) - 1u;
    return (value >> offset) & mask;
}

uint BitExtract(uint2 value, uint offset, uint length) {
    const uint mask = (1u << length) - 1u;
    if (offset < 32) {
        return (value.x >> offset) & mask;
    } else {
        return (value.y >> (offset - 32)) & mask;
    }
}

int SignExtend(int value, int bits) {
    const uint shift = 32 - bits;
    return (value << shift) >> shift;
}

int2 Extract16PairSX(uint value32, int bits) {
    return int2(
        SignExtend(BitExtract(value32, 0, 16), bits),
        SignExtend(BitExtract(value32, 16, 16), bits)
    );
}

int2 Extract16PairS(uint value32) {
    return Extract16PairSX(value32, 16);
}

uint ByteSwap16(uint val) {
    return ((val >> 8) & 0x00FF) |
           ((val << 8) & 0xFF00);
}

static const uint interlaceMode = BitExtract(config.displayParams, 0, 2);
static const uint oddField = BitExtract(config.displayParams, 2, 1);
static const bool exclusiveMonitor = BitTest(config.displayParams, 3);
static const uint spriteDisplayFB = BitExtract(config.displayParams, 29, 1);
static const uint spriteFBBaseOffset = spriteDisplayFB * 256 * 1024;

uint ReadSprite8(uint address) {
    address += spriteFBBaseOffset;
    return BitExtract(spriteFB.Load(address & ~3), (address & 3) * 8, 8);
}

uint ReadSprite16(uint address) {
    address += spriteFBBaseOffset;
    return ByteSwap16(BitExtract(spriteFB.Load(address & ~3), (address & 2) * 8, 16));
}

uint GetY(uint y, bool doubleDensityOnly) {
    const bool interlaced = doubleDensityOnly ? interlaceMode == 3 : interlaceMode >= 2;
    if (interlaced && !exclusiveMonitor) {
        return (y << 1) | (oddField /* TODO & !deinterlace */);
    } else {
        return y;
    }
}

uint4 FetchCRAMColor(uint cramOffset, uint colorIndex) {
    const uint cramAddress = (cramOffset + colorIndex) & kCRAMAddressMask;
    return cramColor[cramAddress];
}

uint4 Color555(uint val16) {
    return uint4(
        ((val16 >> 0) & 0x1F) << 3,
        ((val16 >> 5) & 0x1F) << 3,
        ((val16 >> 10) & 0x1F) << 3,
        (val16 >> 15) & 1
    );
}

// -----------------------------------------------------------------------------

static const uint kSpriteDataNormal = 0; // Any other value
static const uint kSpriteDataShadow = 1; // Normal shadow pattern (DC=0b...11110)
static const uint kSpriteDataTransparent = 2; // Raw 16-bit value is 0x0000

struct SpriteData {
    uint colorData; // DC10-0
    uint colorCalcRatio; // CC2-0
    uint priority; // PR2-0
    bool shadowOrWindow; // SD
    uint special; // Color data special patterns
};

uint GetSpecialPattern(uint rawData, uint colorDataBits) {
    // Normal shadow pattern (LSB = 0, rest of the color data bits = 1)
    const uint kNormalShadowValue = (1u << colorDataBits) - 2u;

    if ((rawData & 0x7FFF) == 0) {
        return kSpriteDataTransparent;
    } else if (BitExtract(rawData, 0, colorDataBits) == kNormalShadowValue) {
        return kSpriteDataShadow;
    } else {
        return kSpriteDataNormal;
    }
}

SpriteData FetchSpriteData(uint fbAddr) {
    // Adjust offset based on VDP1 data size.
    // The majority of games actually set the sprite readout size to match the VDP1 sprite data size, but there's
    // *always* an exception...
    // 8-bit VDP1 data vs. 16-bit readout: NBA Live 98
    // 16-bit VDP1 data vs. 8-bit readout: I Love Donald Duck
    const bool pixel8Bits = BitTest(config.displayParams, 8);
    const uint type = BitExtract(config.displayParams, 9, 4);
    uint rawData;
    if (pixel8Bits) {
        rawData = ReadSprite8(fbAddr);
        if (type < 8 /*&& (!applyMesh || rawData != 0)*/) {
            rawData |= 0xFF00;
        }
    } else {
        fbAddr <<= 1;
        rawData = ReadSprite16(fbAddr);
    }

    // Sprite types 0-7 are 16-bit, 8-15 are 8-bit

    SpriteData data;
    switch (type) {
        case 0x0:
            data.colorData = BitExtract(rawData, 0, 11);
            data.colorCalcRatio = BitExtract(rawData, 11, 3);
            data.priority = BitExtract(rawData, 14, 2);
            data.shadowOrWindow = false;
            data.special = GetSpecialPattern(rawData, 11);
            break;

        case 0x1:
            data.colorData = BitExtract(rawData, 0, 11);
            data.colorCalcRatio = BitExtract(rawData, 11, 2);
            data.priority = BitExtract(rawData, 13, 33);
            data.shadowOrWindow = false;
            data.special = GetSpecialPattern(rawData, 11);
            break;

        case 0x2:
            data.colorData = BitExtract(rawData, 0, 11);
            data.colorCalcRatio = BitExtract(rawData, 11, 3);
            data.priority = BitExtract(rawData, 14, 1);
            data.shadowOrWindow = BitTest(rawData, 15);
            data.special = GetSpecialPattern(rawData, 11);
            break;

        case 0x3:
            data.colorData = BitExtract(rawData, 0, 11);
            data.colorCalcRatio = BitExtract(rawData, 11, 2);
            data.priority = BitExtract(rawData, 13, 2);
            data.shadowOrWindow = BitTest(rawData, 15);
            data.special = GetSpecialPattern(rawData, 11);
            break;

        case 0x4:
            data.colorData = BitExtract(rawData, 0, 10);
            data.colorCalcRatio = BitExtract(rawData, 10, 3);
            data.priority = BitExtract(rawData, 13, 2);
            data.shadowOrWindow = BitTest(rawData, 15);
            data.special = GetSpecialPattern(rawData, 10);
            break;

        case 0x5:
            data.colorData = BitExtract(rawData, 0, 11);
            data.colorCalcRatio = BitExtract(rawData, 11, 1);
            data.priority = BitExtract(rawData, 12, 3);
            data.shadowOrWindow = BitTest(rawData, 15);
            data.special = GetSpecialPattern(rawData, 11);
            break;

        case 0x6:
            data.colorData = BitExtract(rawData, 0, 10);
            data.colorCalcRatio = BitExtract(rawData, 10, 2);
            data.priority = BitExtract(rawData, 12, 3);
            data.shadowOrWindow = BitTest(rawData, 15);
            data.special = GetSpecialPattern(rawData, 10);
            break;

        case 0x7:
            data.colorData = BitExtract(rawData, 0, 9);
            data.colorCalcRatio = BitExtract(rawData, 9, 3);
            data.priority = BitExtract(rawData, 12, 3);
            data.shadowOrWindow = BitTest(rawData, 15);
            data.special = GetSpecialPattern(rawData, 9);
            break;

        case 0x8:
            data.colorData = BitExtract(rawData, 0, 7);
            data.colorCalcRatio = 0;
            data.priority = BitExtract(rawData, 7, 1);
            data.shadowOrWindow = false;
            data.special = GetSpecialPattern(rawData, 7);
            break;

        case 0x9:
            data.colorData = BitExtract(rawData, 0, 6);
            data.colorCalcRatio = BitExtract(rawData, 6, 1);
            data.priority = BitExtract(rawData, 7, 1);
            data.shadowOrWindow = false;
            data.special = GetSpecialPattern(rawData, 6);
            break;

        case 0xA:
            data.colorData = BitExtract(rawData, 0, 6);
            data.colorCalcRatio = 0;
            data.priority = BitExtract(rawData, 6, 2);
            data.shadowOrWindow = false;
            data.special = GetSpecialPattern(rawData, 6);
            break;

        case 0xB:
            data.colorData = BitExtract(rawData, 0, 6);
            data.colorCalcRatio = BitExtract(rawData, 6, 2);
            data.priority = 0;
            data.shadowOrWindow = false;
            data.special = GetSpecialPattern(rawData, 6);
            break;

        case 0xC:
            data.colorData = BitExtract(rawData, 0, 8);
            data.colorCalcRatio = 0;
            data.priority = BitExtract(rawData, 7, 1);
            data.shadowOrWindow = false;
            data.special = GetSpecialPattern(rawData, 8);
            break;

        case 0xD:
            data.colorData = BitExtract(rawData, 0, 8);
            data.colorCalcRatio = BitExtract(rawData, 6, 1);
            data.priority = BitExtract(rawData, 7, 1);
            data.shadowOrWindow = false;
            data.special = GetSpecialPattern(rawData, 8);
            break;

        case 0xE:
            data.colorData = BitExtract(rawData, 0, 8);
            data.colorCalcRatio = 0;
            data.priority = BitExtract(rawData, 6, 2);
            data.shadowOrWindow = false;
            data.special = GetSpecialPattern(rawData, 8);
            break;

        case 0xF:
            data.colorData = BitExtract(rawData, 0, 8);
            data.colorCalcRatio = BitExtract(rawData, 6, 2);
            data.priority = 0;
            data.shadowOrWindow = false;
            data.special = GetSpecialPattern(rawData, 8);
            break;
    }
    return data;
}

// index 0 = sprite
// index 1 = transparent meshes
uint4 DrawSprite(uint2 pos, uint index) {
    // TODO: implement transparent meshes
    if (index == 1) {
        return kTransparentPixel;
    }

    const bool rotate = BitTest(config.displayParams, 7);
    const uint type = BitExtract(config.displayParams, 9, 4);
    const uint fbSizeH = 512 << BitExtract(config.displayParams, 13, 1);
    const bool inHalfResH = BitTest(config.displayParams, 14);
    const bool outHalfResH = BitTest(config.displayParams, 15);
    const bool mixedFormat = BitTest(config.displayParams, 16);
    const bool useSpriteWindow = BitTest(config.displayParams, 17);

    const uint2 spritePos = rotate ? Extract16PairS(rotParamState[0].spriteCoords) :
                            inHalfResH ? uint2(pos.x << 1, pos.y) :
                            outHalfResH ? uint2(pos.x >> 1, pos.y) :
                            pos;
    const uint2 outPos = uint2(pos.x, GetY(pos.y, false));
    const uint fbAddr = spritePos.x + spritePos.y * fbSizeH;

    // TODO: sprite window
    /*if (m_spriteLayerAttrs[altField].window[x]) {
        return kTransparentPixel;
    }*/

    if (mixedFormat) {
        const uint spriteDataValue = ReadSprite16(fbAddr << 1);
        if (BitTest(spriteDataValue, 15)) {
            // RGB data

            // Transparent if:
            // - Using byte-sized sprite types (0x8 to 0xF) and the lower 8 bits are all zero
            // - Using word-sized sprite types that have the shadow/sprite window bit (types 0x2 to 0x7), sprite
            //   window is enabled, and the lower 15 bits are all zero
            if (type >= 8) {
                if (BitExtract(spriteDataValue, 0, 8) == 0) {
                    return kTransparentPixel;
                }
            } else if (type >= 2) {
                if (useSpriteWindow && BitExtract(spriteDataValue, 0, 15) == 0) {
                    return kTransparentPixel;
                }
            }

            const uint4 outColor = Color555(spriteDataValue);
            const uint outPriority = BitExtract(config.spriteParams.x, 0, 3);

            spriteAttrsOut[outPos] = BitExtract(config.spriteParams, 0 * 8 + 3, 5);
            return uint4(outColor.rgb, outPriority);
        }
    }

    // Palette data
    const SpriteData spriteData = FetchSpriteData(fbAddr);

    // Handle sprite window
    const bool spriteWindowEnabled = BitTest(config.displayParams, 27);
    const bool spriteWindowInverted = BitTest(config.displayParams, 28);
    if (useSpriteWindow && spriteWindowEnabled && spriteData.shadowOrWindow != spriteWindowInverted) {
        uint4 outPixel = kTransparentPixel;
        outPixel.a |= 1 << kPixelAttrBitSpriteShadowWindow;
        return outPixel;
    }

    const uint colorDataOffset = BitExtract(config.displayParams, 24, 3) << 8;
    const uint colorIndex = colorDataOffset + spriteData.colorData;
    const uint4 outColor = FetchCRAMColor(0, colorIndex);
    const uint outTransparent = (spriteData.special == kSpriteDataTransparent) ? 1 : 0;
    const uint outShadowOrWindow = spriteData.shadowOrWindow ? 1 : 0;
    const uint outNormalShadow = (spriteData.special == kSpriteDataShadow) ? 1 : 0;
    const uint outPriority = BitExtract(config.spriteParams, spriteData.priority * 8, 3);
    const uint outMSB = outColor.a;

    spriteAttrsOut[outPos] = BitExtract(config.spriteParams, spriteData.colorCalcRatio * 8 + 3, 5);
    return uint4(
        outColor.rgb,
        (outTransparent << kPixelAttrBitTransparent) |
        (1 << kPixelAttrBitSpecColorCalc) |
        (outNormalShadow << kPixelAttrBitSpriteNormalShadow) |
        (outShadowOrWindow << kPixelAttrBitSpriteShadowWindow) |
        (outMSB << kPixelAttrBitSpriteColorMSB) |
        outPriority
    );
}

// -----------------------------------------------------------------------------

[numthreads(32, 1, 2)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint2 drawCoord = uint2(id.x, id.y + config.startY);
    const uint3 outCoord = uint3(drawCoord.x, GetY(drawCoord.y, false), id.z + 6);
    bgOut[outCoord] = DrawSprite(drawCoord, id.z);
}
