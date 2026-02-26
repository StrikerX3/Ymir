struct Config {
    uint numPolys;
    uint params;
    uint erase;
    uint _reserved;
};

struct PolyParams {
    uint atlasPos;
    uint size;
    uint fbPos;
    uint sysClip;
    uint userClipX;
    uint userClipY;
    uint localCoord;
    uint cmdAddress;
};

// -----------------------------------------------------------------------------

cbuffer Config : register(b0) {
    Config config;
}

ByteAddressBuffer vram : register(t0);
StructuredBuffer<PolyParams> polyParams : register(t1);

RWByteAddressBuffer polyAtlas : register(u0);

// -----------------------------------------------------------------------------

static const uint kOffsetCMDCTRL = 0x00;
static const uint kOffsetCMDLINK = 0x02;
static const uint kOffsetCMDPMOD = 0x04;
static const uint kOffsetCMDCOLR = 0x06;
static const uint kOffsetCMDSRCA = 0x08;
static const uint kOffsetCMDSIZE = 0x0A;
static const uint kOffsetCMDXA = 0x0C;
static const uint kOffsetCMDYA = 0x0E;
static const uint kOffsetCMDXB = 0x10;
static const uint kOffsetCMDYB = 0x12;
static const uint kOffsetCMDXC = 0x14;
static const uint kOffsetCMDYC = 0x16;
static const uint kOffsetCMDXD = 0x18;
static const uint kOffsetCMDYD = 0x1A;
static const uint kOffsetCMDGRDA = 0x1C;

static const uint kCommandDrawNormalSprite = 0x0;
static const uint kCommandDrawScaledSprite = 0x1;
static const uint kCommandDrawDistortedSprite = 0x2;
static const uint kCommandDrawDistortedSpriteAlt = 0x3;
static const uint kCommandDrawPolygon = 0x4;
static const uint kCommandDrawPolylines = 0x5;
static const uint kCommandDrawPolylinesAlt = 0x7;
static const uint kCommandDrawLine = 0x6;
// No other commands should hit the renderer

static const uint kAtlasStride = 2048;

// Special values for the polygon merger
//  bits  use
//    31  Pixel drawn
//    18  Mesh pixel
// 17-16  Color calculation mode to apply
//          0 = replace
//          1 = shadow
//          2 = half-luminance
//          3 = half-transparency
//  15-0  Raw color data
static const uint kPolyMergerPixelDrawn = 31;
static const uint kPolyMergerMesh = 18;
static const uint kPolyMergerColorCalcBitsShift = 16;
static const uint kPolyMergerSetMSB = 0xFFFFFFFF;

struct CMDPMOD_COLR {
    // CMDPMOD
    uint colorCalcBits;
    bool gouraudEnable;
    uint colorMode;
    bool transparentPixelDisable;
    bool endCodeDisable;
    bool meshEnable;
    bool clippingMode;
    bool userClippingEnable;
    bool preClippingDisable;
    bool highSpeedShrink;
    bool msbOn;

    // CMDCOLR
    uint color;
};

struct CMDSRCA_SIZE {
    uint charAddress;
    uint2 charSize;
};

typedef uint4 Color555;

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

uint2 Extract16PairU(uint value32) {
    return uint2(
        BitExtract(value32, 0, 16),
        BitExtract(value32, 16, 16)
    );
}

int2 Extract16PairSX(uint value32, int bits) {
    return uint2(
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

uint ByteSwap32(uint val) {
    return ((val >> 24) & 0x000000FF) |
           ((val >> 8) & 0x0000FF00) |
           ((val << 8) & 0x00FF0000) |
           ((val << 24) & 0xFF000000);
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

// offset must be 16-bit aligned
uint FetchCMDSingle(uint cmdAddress, uint offset) {
    return ReadVRAM16(cmdAddress + offset);
}

// offset must be 32-bit aligned
uint2 FetchCMDPair(uint cmdAddress, uint offset) {
    const uint value = ReadVRAM32(cmdAddress + offset);
    return Extract16PairU(value);
}

uint FetchCMDCTRL(uint cmdAddress) {
    return FetchCMDSingle(cmdAddress, kOffsetCMDCTRL);
}

CMDPMOD_COLR FetchCMDPMOD_COLR(uint cmdAddress) {
    const uint2 pair = FetchCMDPair(cmdAddress, kOffsetCMDPMOD);
    
    CMDPMOD_COLR data;
    data.colorCalcBits = BitExtract(pair.y, 0, 2);
    data.gouraudEnable = BitTest(pair.y, 2);
    data.colorMode = BitExtract(pair.y, 3, 3);
    data.transparentPixelDisable = BitTest(pair.y, 6);
    data.endCodeDisable = BitTest(pair.y, 7);
    data.meshEnable = BitTest(pair.y, 8);
    data.clippingMode = BitTest(pair.y, 9);
    data.userClippingEnable = BitTest(pair.y, 10);
    data.preClippingDisable = BitTest(pair.y, 11);
    data.highSpeedShrink = BitTest(pair.y, 12);
    data.msbOn = BitTest(pair.y, 15);
    
    data.color = pair.x;
    return data;
}

CMDSRCA_SIZE FetchCMDSRCA_SIZE(uint cmdAddress) {
    const uint2 pair = FetchCMDPair(cmdAddress, kOffsetCMDSRCA);
    
    CMDSRCA_SIZE data;
    data.charAddress = pair.y << 3;
    data.charSize.x = max(BitExtract(pair.x, 8, 6), 1);
    data.charSize.y = max(BitExtract(pair.x, 0, 8) << 8, 1);
    return data;
}

int2 FetchCMDXA_YA(uint cmdAddress) {
    const uint2 raw = FetchCMDPair(cmdAddress, kOffsetCMDXA);
    return int2(SignExtend(raw.y, 13), SignExtend(raw.x, 13));
}

int2 FetchCMDXB_YB(uint cmdAddress) {
    const uint2 raw = FetchCMDPair(cmdAddress, kOffsetCMDXB);
    return int2(SignExtend(raw.y, 13), SignExtend(raw.x, 13));
}

int2 FetchCMDXC_YC(uint cmdAddress) {
    const uint2 raw = FetchCMDPair(cmdAddress, kOffsetCMDXC);
    return int2(SignExtend(raw.y, 13), SignExtend(raw.x, 13));
}

int2 FetchCMDXD_YD(uint cmdAddress) {
    const uint2 raw = FetchCMDPair(cmdAddress, kOffsetCMDXD);
    return int2(SignExtend(raw.y, 13), SignExtend(raw.x, 13));
}

uint FetchCMDGRDA(uint cmdAddress) {
    return FetchCMDSingle(cmdAddress, kOffsetCMDGRDA) << 3;
}

Color555 Uint16ToColor555(uint rawValue) {
    return uint4(
        BitExtract(rawValue, 0, 5),
        BitExtract(rawValue, 5, 5),
        BitExtract(rawValue, 10, 5),
        BitExtract(rawValue, 15, 1)
    );
}

uint Color555ToUint16(Color555 color) {
    return color.r | (color.g << 5) | (color.b << 10) | (color.a << 15);
}

// -----------------------------------------------------------------------------

static const uint fbSizeH = 512 << BitExtract(config.params, 0, 1);
static const bool pixel8Bits = BitTest(config.params, 2);
static const bool doubleDensity = BitTest(config.params, 3);
static const bool dblInterlaceEnable = BitTest(config.params, 4);
static const bool dblInterlaceDrawLine = BitTest(config.params, 5);
static const bool deinterlace = false;

// Steps over the texels of a texture.
struct TextureStepper {
    int num;
    int den;
    int accum;

    int value;
    int inc;

    void Setup(uint length, int start, int end, bool hss = false, int hssSelect = 0) {
        if (hss) {
            start >>= 1;
            end >>= 1;
        }
        const int delta = end - start;
        const uint absDelta = abs(delta);

        value = start;
        inc = delta >= 0 ? +1 : -1;
        if (hss) {
            value <<= 1;
            value |= hssSelect;
            inc <<= 1;
        }

        num = absDelta;
        den = length;
        if (length <= absDelta) {
            ++num;
            accum = absDelta - (length << 1);
            if (delta >= 0) {
                ++accum;
            }
        } else {
            --den;
            accum = length - (length << 1);
            if (delta < 0) {
                ++accum;
            }
        }
        num <<= 1;
        den <<= 1;
    }

    // Retrieves the current texture coordinate value.
    uint Value() {
        return value;
    }

    // Determines if the stepper is ready to step to the next texel.
    bool ShouldStepTexel() {
        return accum >= 0;
    }

    // Steps to the next texel.
    void StepTexel() {
        value += inc;
        accum -= den;
    }

    // Advances to the next pixel.
    void StepPixel() {
        accum += num;
    }

    // Skips the specified number of pixels.
    // Meant to be used when clipping lines with LineStepper::SystemClip().
    void SkipPixels(uint count) {
        accum += num * count;
    }
};

// -----------------------------------------------------------------------------

// Iterates over a gouraud gradient of a single color channel.
struct GouraudChannelStepper {
    int num;
    int den;
    int accum;

    int value;
    int intInc;
    int fracInc;

    void Setup(uint length, uint start, uint end) {
        const int delta = end - start;
        const uint absDelta = abs(delta);

        value = start;
        intInc = 0;
        fracInc = delta >= 0 ? +1 : -1;

        num = absDelta;
        den = length;
        if (length <= absDelta) {
            ++num;
            accum = absDelta - (length << 1);
            if (delta >= 0) {
                ++accum;
            }
        } else {
            --den;
            accum = -int(length);
            if (delta < 0) {
                ++accum;
            }
        }
        num <<= 1;
        den <<= 1;

        if (den != 0) {
            while (accum >= 0) {
                value += fracInc;
                accum -= den;
            }

            while (num >= den) {
                intInc += fracInc;
                num -= den;
            }
        }
        accum = ~accum;
    }

    // Advances the gradient by a single pixel.
    void Step() {
        value += intInc;
        accum -= num;
        if (accum < 0) {
            value += fracInc;
            accum += den;
        }
    }

    // Skips the specified number of pixels.
    // Meant to be used when clipping lines with LineStepper::SystemClip().
    void Skip(uint steps) {
        value += intInc * steps;
        accum -= num * steps;
        if (den != 0) {
            while (accum < 0) {
                value += fracInc;
                accum += den;
            }
        }
    }

    // Retrieves the current gouraud shading value.
    uint Value() {
        return value;
    }

    // Blends the given base color value with the current gouraud shading value.
    // The color value must be a 5-bit value.
    uint Blend(int color) {
        return clamp(value + color - 16, 0, 31);
    }
};

// -----------------------------------------------------------------------------

struct GouraudStepper {
    GouraudChannelStepper stepperR;
    GouraudChannelStepper stepperG;
    GouraudChannelStepper stepperB;

    // Sets up gouraud shading with the given length and start and end colors.
    void Setup(uint length, Color555 gouraudStart, Color555 gouraudEnd) {
        stepperR.Setup(length, gouraudStart.r, gouraudEnd.r);
        stepperG.Setup(length, gouraudStart.g, gouraudEnd.g);
        stepperB.Setup(length, gouraudStart.b, gouraudEnd.b);
    }

    // Steps the gouraud shader to the next coordinate.
    void Step() {
        stepperR.Step();
        stepperG.Step();
        stepperB.Step();
    }

    // Skips the specified number of pixels.
    // Meant to be used when clipping lines with LineStepper::SystemClip().
    void Skip(uint steps) {
        if (steps > 0) {
            stepperR.Skip(steps);
            stepperG.Skip(steps);
            stepperB.Skip(steps);
        }
    }

    // Returns the current gouraud gradient value.
    Color555 Value() {
        return uint4(
            stepperR.Value(),
            stepperG.Value(),
            stepperB.Value(),
            0
        );
    }

    // Blends the given base color with the current gouraud shading values.
    Color555 Blend(Color555 baseColor) {
        return uint4(
            stepperR.Blend(baseColor.r),
            stepperG.Blend(baseColor.g),
            stepperB.Blend(baseColor.b),
            baseColor.a
        );
    }
};

// -----------------------------------------------------------------------------

struct LineStepper {
    int num;
    int den;
    int accum;
    int accumTarget;
    
    int2 majInc;
    int2 minInc;
    
    int2 pos;
    int2 end;
    
    uint dmaj;
    uint step;
    uint length;
    
    int2 aaInc;
    bool antiAlias;
    
    // Clips the slope to the area 0x0..width x height.
    // Returns the number of increments skipped.
    uint SystemClip(int2 size) {
        static const int kPadding = 1;
        
        // Add padding to compensate for minor inaccuracies
        size += kPadding + 1;
        
        // Bail out early if the line length is zero
        uint length = dmaj;
        if (length == 0) {
            return 0;
        }

        int2 posS = pos;
        int2 posE = end;
        
        // Bail out early if the line is entirely in-bounds
        if (all(posS >= -kPadding) && all(posS <= size) &&
            all(posE >= -kPadding) && all(posE <= size)) {
            return 0;
        }

        // Fully clip line if it is entirely out of bounds
        if ((posS.x < -kPadding && posE.x < -kPadding) || (posS.x > size.x && posE.x > size.x) ||
            (posS.y < -kPadding && posE.y < -kPadding) || (posS.y > size.y && posE.y > size.y)) {
            pos = end;
            length = 0;
            return 0;
        }

        // ---------------------------------------------------------------------
        
        const bool xmajor = majInc.x != 0;
        const int inc = majInc.x + majInc.y; // only one of the two is non-zero
        const int startPos = xmajor ? posS.x : posS.y;
        const int endPos = xmajor ? posE.x : posE.y;
        const int maxLen = xmajor ? size.x : size.y;

        // Clip from the start
        uint startClip = 0u;
        if (inc > 0 && startPos < -kPadding) {
            startClip = -startPos - 1 - kPadding;
        } else if (inc < 0 && startPos > maxLen) {
            startClip = startPos - maxLen;
        }
        if (startClip > 0u) {
            startClip = min(startClip, length - 1u);
            pos += majInc * startClip;
            for (uint i = 0; i < startClip; ++i) {
                accum -= num;
                if (den != 0) {
                    if (accum <= accumTarget) {
                        const uint steps = (accumTarget - accum + 1) / den;
                        accum += den * steps;
                        pos += minInc * steps;
                    }
                }
            }
            length -= startClip;
        }
        
        
        // ---------------------------------------------------------------------

        // Clip from the end
        uint endClip = 0u;
        if (inc < 0 && endPos < -kPadding) {
            endClip = -endPos - 1 - kPadding;
        } else if (inc > 0 && endPos > maxLen) {
            endClip = endPos - maxLen;
        }
        endClip = min(endClip, length);
        if (endClip > 0u) {
            endClip = length - endClip;
            length = endClip;
            end = pos;
            int tempAccum = accum;
            for (uint i = 0; i < endClip; ++i) {
                tempAccum -= num;
                if (den != 0) {
                    if (tempAccum <= accumTarget) {
                        const uint steps = (accumTarget - tempAccum + 1) / den;
                        tempAccum += den * steps;
                        end += minInc * steps;
                    }
                }
            }
        }
        length = length + 1u;

        return startClip;
    }

    // Determines if the slope can be stepped.
    bool CanStep() {
        return step <= length;
    }
    
    // Steps the slope to the next coordinate.
    // Should not be invoked when CanStep() returns false.
    // Returns true if an antialias pixel should be drawn.
    bool Step() {
        ++step;
        pos += majInc;
        accum -= num;
        if (accum <= accumTarget) {
            accum += den;
            pos += minInc;
            return antiAlias;
        }
        return false;
    }

    // Retrieves the current X and Y coordinates.
    int2 Coord() {
        return pos & 0x7FF;
    }

    // Returns the X and Y coordinates of the antialiased pixel.
    int2 AACoord() {
        return pos + aaInc;
    }

    // Retrieves the total number of steps in the slope, that is, the longest of the vertical and horizontal spans.
    uint Length() {
        return dmaj;
    }
};

LineStepper NewLineStepper(int2 coord1, int2 coord2, bool antiAlias = false) {
    LineStepper stepper;
    
    stepper.pos = coord1;
    stepper.end = coord2;
    stepper.antiAlias = antiAlias;
    
    int2 delta = coord2 - coord1;
    int2 absDelta = abs(delta); // component-wise
    stepper.dmaj = max(absDelta.x, absDelta.y);
    stepper.length = stepper.dmaj + 1;
    stepper.step = 0;
    
    const bool xMajor = absDelta.x >= absDelta.y;
    if (xMajor) {
        stepper.majInc.x = delta.x >= 0 ? +1 : -1;
        stepper.majInc.y = 0;
        stepper.minInc.x = 0;
        stepper.minInc.y = delta.y >= 0 ? +1 : -1;
    } else {
        stepper.majInc.x = 0;
        stepper.majInc.y = delta.y >= 0 ? +1 : -1;
        stepper.minInc.x = delta.x >= 0 ? +1 : -1;
        stepper.minInc.y = 0;
        delta.xy = delta.yx;
        absDelta.xy = absDelta.yx;
    }
    stepper.num = absDelta.y << 1;
    stepper.den = absDelta.x << 1;
    stepper.accum = absDelta.x + 1;
    stepper.accumTarget = 0;
    if (!antiAlias && delta.x < 0) {
        ++stepper.accumTarget;
    }
    stepper.accum += stepper.num;
    
    stepper.pos -= stepper.majInc;
    
    if (antiAlias) {
        --stepper.accum;
        --stepper.accumTarget;
        const bool samesign = (coord1.x > coord2.x) == (coord1.y > coord2.y);
        if (xMajor) {
            stepper.aaInc.x = samesign ? 0 : -stepper.majInc.x;
            stepper.aaInc.y = samesign ? -stepper.minInc.y : 0;
        } else {
            stepper.aaInc.x = samesign ? 0 : -stepper.minInc.x;
            stepper.aaInc.y = samesign ? -stepper.majInc.y : 0;
        }
    }
    
    // NOTE: Shifting counters by this amount forces them to have 13 bits without the need for masking
    static const int kShift = 32 - 13;
    
    stepper.num <<= kShift;
    stepper.den <<= kShift;
    stepper.accum <<= kShift;
    stepper.accumTarget <<= kShift;
    
    return stepper;
}

// -----------------------------------------------------------------------------

struct Edge {
    GouraudStepper gouraud;
    bool gouraudEnable;

    uint dmaj;

    int2 pos;
    int2 posInc;
    int2 posNum;
    int2 posDen;
    int2 posAccum;
    int2 posAccumTarget;

    int num;
    int den;
    int accum;
    int accumTarget;
    
    void Setup(int2 coord1, int2 coord2, uint delta) {
        const int dx = SignExtend(coord2.x - coord1.x, 13);
        const int dy = SignExtend(coord2.y - coord1.y, 13);
        const uint adx = abs(dx);
        const uint ady = abs(dy);
        dmaj = max(adx, ady);

        pos = coord1;

        posInc.x = dx >= 0 ? +1 : -1;
        posInc.y = dy >= 0 ? +1 : -1;

        posNum.x = adx << 1;
        posNum.y = ady << 1;

        posDen.x = posDen.y = dmaj << 1;
        posAccum.x = posAccum.y = ~dmaj;

        posAccumTarget.x = dy < 0 ? -1 : 0;
        posAccumTarget.y = dx < 0 ? -1 : 0;

        num = dmaj << 1;
        den = delta << 1;
        accum = ~delta;
        accumTarget = adx >= ady ? posAccumTarget.y : posAccumTarget.x;

        // NOTE: Shifting counters by this amount forces them to have 13 bits without the need for masking
        static const int kShift = 32 - 13;

        posNum <<= kShift;
        posDen <<= kShift;
        posAccum <<= kShift;
        posAccumTarget <<= kShift;

        num <<= kShift;
        den <<= kShift;
        accum <<= kShift;
        accumTarget <<= kShift;

        gouraudEnable = false;
    }
    
    void SetupGouraud(Color555 start, Color555 end) {
        gouraud.Setup(dmaj + 1, start, end);
        gouraudEnable = true;
    }
    
    void Step() {
        accum += num;
        if (accum >= accumTarget) {
            accum -= den;

            posAccum.x += posNum.x;
            if (posAccum.x >= posAccumTarget.x) {
                posAccum.x -= posDen.x;
                pos.x += posInc.x;
            }

            posAccum.y += posNum.y;
            if (posAccum.y >= posAccumTarget.y) {
                posAccum.y -= posDen.y;
                pos.y += posInc.y;
            }

            if (gouraudEnable) {
                gouraud.Step();
            }
        }
    }
    
    // Retrieves the current X and Y coordinates of this edge.
    int2 Coord() {
        return pos;
    }
    
    Color555 GouraudValue() {
        return gouraud.Value();
    }
};

// -----------------------------------------------------------------------------

// Dual edge iterator for a quad with vertices A-B-C-D arranged in clockwise order from top-left:
//
//    A-->B
//    ^   |
//    |   v
//    D<--C
//
// The stepper uses the edges A-D and B-C and steps over each pixel on the longer edge, advancing the position on the
// other edge proportional to their lengths.
struct QuadStepper {
    Edge edgeL; // left edge (A-D)
    Edge edgeR; // right edge (B-C)

    uint dmaj;
    uint step;

    // Sets up texture interpolation for the given texture vertical size and parameters.
    void SetupTexture(inout TextureStepper stepper, uint charSizeV, bool flipV) {
        int start = 0;
        int end = charSizeV - 1;
        if (flipV) {
            int tmp = start;
            start = end;
            end = tmp;
        }
        stepper.Setup(dmaj + 1, start, end);
    }

    // Sets up gouraud shading with the given start and end values.
    void SetupGouraud(Color555 colorA, Color555 colorB, Color555 colorC, Color555 colorD) {
        edgeL.SetupGouraud(colorA, colorD);
        edgeR.SetupGouraud(colorB, colorC);
    }

    // Determines if this stepper can be stepped.
    bool CanStep() {
        return step <= dmaj;
    }

    // Steps both edges of the quad to the next coordinate.
    // The major edge is stepped by a full pixel.
    // The minor edge is stepped in proportion to the major edge.
    // Should not be invoked when CanStep() returns false.
    void Step() {
        ++step;

        edgeL.Step();
        edgeR.Step();
    }
};

QuadStepper NewQuadStepper(int2 coordA, int2 coordB, int2 coordC, int2 coordD) {
    QuadStepper stepper;

    const uint deltaLx = abs(SignExtend(coordD.x - coordA.x, 13));
    const uint deltaLy = abs(SignExtend(coordD.y - coordA.y, 13));
    const uint deltaRx = abs(SignExtend(coordC.x - coordB.x, 13));
    const uint deltaRy = abs(SignExtend(coordC.y - coordB.y, 13));

    stepper.dmaj = max(max(deltaLx, deltaLy), max(deltaRx, deltaRy)) & 0xFFF;
    stepper.step = 0;

    stepper.edgeL.Setup(coordA, coordD, stepper.dmaj);
    stepper.edgeR.Setup(coordB, coordC, stepper.dmaj);
    
    return stepper;
}

// -----------------------------------------------------------------------------

struct LineParams {
    CMDPMOD_COLR mode_color;
    Color555 gouraudLeft;
    Color555 gouraudRight;
};

struct PixelParams {
    CMDPMOD_COLR mode_color;
    GouraudStepper gouraud;
};

struct TexturedLineParams {
    uint control;
    CMDPMOD_COLR mode_color;
    CMDSRCA_SIZE srca_size;
    TextureStepper texVStepper;
};

bool IsPixelUserClipped(const PolyParams poly, int2 coord) {
    const int userClipX0 = BitExtract(poly.userClipX, 0, 16);
    const int userClipX1 = BitExtract(poly.userClipX, 16, 16);
    const int userClipY0 = BitExtract(poly.userClipY, 0, 16);
    const int userClipY1 = BitExtract(poly.userClipY, 16, 16);
    if (coord.x < userClipX0 || coord.x > userClipX1) {
        return true;
    }
    if (coord.y < userClipY0 || coord.y > userClipY1) {
        return true;
    }
    return false;
}

bool IsPixelSystemClipped(const PolyParams poly, int2 coord) {
    const int sysClipH = BitExtract(poly.sysClip, 0, 16);
    const int sysClipV = BitExtract(poly.sysClip, 16, 16);
    if (coord.x < 0 || coord.x > sysClipH) {
        return true;
    }
    if (coord.y < 0 || coord.y > sysClipV) {
        return true;
    }
    return false;
}

bool IsLineSystemClipped(const PolyParams poly, int2 coord1, int2 coord2) {
    const int sysClipH = BitExtract(poly.sysClip, 0, 16);
    const int sysClipV = BitExtract(poly.sysClip, 16, 16);
    if (coord1.x < 0 && coord2.x < 0) {
        return true;
    }
    if (coord1.y < 0 && coord2.y < 0) {
        return true;
    }
    if (coord1.x > sysClipH && coord2.x > sysClipH) {
        return true;
    }
    if (coord1.y > sysClipV && coord2.y > sysClipV) {
        return true;
    }
    return false;
}

bool IsPixelClipped(const PolyParams poly, int2 coord, bool userClippingEnable, bool clippingMode) {
    if (IsPixelSystemClipped(poly, coord)) {
        return true;
    }
    if (userClippingEnable) {
        // clippingMode = false -> draw inside, reject outside
        // clippingMode = true -> draw outside, reject inside
        // The function returns true if the pixel is clipped, therefore we want to reject pixels that return the
        // opposite of clippingMode on that function.
        if (IsPixelUserClipped(poly, coord) != clippingMode) {
            return true;
        }
    }
    return false;
}

bool IsQuadSystemClipped(const PolyParams poly, int2 coord1, int2 coord2, int2 coord3, int2 coord4) {
    const int sysClipH = BitExtract(poly.sysClip, 0, 16);
    const int sysClipV = BitExtract(poly.sysClip, 16, 16);
    if (coord1.x < 0 && coord2.x < 0 && coord3.x < 0 && coord4.x < 0) {
        return true;
    }
    if (coord1.y < 0 && coord2.y < 0 && coord3.y < 0 && coord4.y < 0) {
        return true;
    }
    if (coord1.x > sysClipH && coord2.x > sysClipH && coord3.x > sysClipH && coord4.x > sysClipH) {
        return true;
    }
    if (coord1.y > sysClipV && coord2.y > sysClipV && coord3.y > sysClipV && coord4.y > sysClipV) {
        return true;
    }
    return false;
}

bool PlotPixel(const PolyParams poly, int2 coord, const PixelParams pixelParams) {
    // Reject pixels outside of clipping area
    if (IsPixelClipped(poly, coord, pixelParams.mode_color.userClippingEnable, pixelParams.mode_color.clippingMode)) {
        return false;
    }
    
    const int2 fbPos = Extract16PairS(poly.fbPos);
    const int2 size = Extract16PairS(poly.size);
    const int2 relPos = coord - fbPos;
    if (any(relPos < 0) || any(relPos >= size)) {
        return false;
    }

    if (pixelParams.mode_color.meshEnable && ((coord.x ^ coord.y) & 1)) {
        return true;
    }

    const bool altFB = deinterlace && doubleDensity && (coord.y & 1);
    if (doubleDensity) {
        if (!deinterlace && dblInterlaceEnable && (coord.y & 1) != dblInterlaceDrawLine) {
            return true;
        }
    }
    if ((deinterlace && doubleDensity) || dblInterlaceEnable) {
        coord.y >>= 1;
    }

    // TODO: pixelParams.mode_color.preClippingDisable
    
    uint value;
    if (pixelParams.mode_color.msbOn) {
        value = kPolyMergerSetMSB;
    } else {
        uint value;
        if (pixel8Bits) {
            // TODO: what happens if pixelParams.mode.colorCalcBits/gouraudEnable != 0?
            value = pixelParams.mode_color.color & 0xFF;
        } else {
            value = pixelParams.mode_color.color;
            if (pixelParams.mode_color.gouraudEnable) {
                // Apply gouraud shading to source color
                Color555 color = Uint16ToColor555(value);
                color = pixelParams.gouraud.Blend(color);
                value = Color555ToUint16(color);
            }
            value |= pixelParams.mode_color.colorCalcBits << kPolyMergerColorCalcBitsShift;
        }
        value |= uint(pixelParams.mode_color.meshEnable) << kPolyMergerMesh;
        value |= 1 << kPolyMergerPixelDrawn;
    }
    
    const int2 baseAtlasPos = Extract16PairS(poly.atlasPos);
    const int2 atlasPos = baseAtlasPos + relPos;
    const uint atlasOffset = (atlasPos.y * kAtlasStride + atlasPos.x) * 4;
    polyAtlas.Store(atlasOffset, value);
    
    return true;
}

bool PlotLine(const PolyParams poly, int2 coord1, int2 coord2, LineParams lineParams, bool antiAlias) {
    if (IsLineSystemClipped(poly, coord1, coord2)) {
        return false;
    }

    const int2 sysClip = int2(
        BitExtract(poly.sysClip, 0, 16),
        BitExtract(poly.sysClip, 16, 16)
    );
    
    LineStepper lineStepper = NewLineStepper(coord1, coord2, antiAlias);
    const uint skipSteps = lineStepper.SystemClip(sysClip);
    PixelParams pixelParams;
    pixelParams.mode_color = lineParams.mode_color;
    if (pixelParams.mode_color.gouraudEnable) {
        pixelParams.gouraud.Setup(lineStepper.Length() + 1, lineParams.gouraudLeft, lineParams.gouraudRight);
        pixelParams.gouraud.Skip(skipSteps);
    }

    bool aa = false;
    bool plotted = false;
    for (lineStepper.Step(); lineStepper.CanStep(); aa = lineStepper.Step()) {
        bool plottedPixel = PlotPixel(poly, lineStepper.Coord(), pixelParams);
        if (antiAlias) {
            if (aa) {
                if (PlotPixel(poly, lineStepper.AACoord(), pixelParams)) {
                    plottedPixel = true;
                }
            }
        }
        if (plottedPixel) {
            plotted = true;
        } else if (plotted) {
            // No more pixels can be drawn past this point
            break;
        }

        if (pixelParams.mode_color.gouraudEnable) {
            pixelParams.gouraud.Step();
        }
    }

    return plotted;
}

struct EndCodeCounter {
    bool enable;
    bool hasEndCode;
    int count;
    
    void ProcessEndCode(bool endCode) {
        if (enable && endCode) {
            hasEndCode = true;
            ++count;
        } else {
            hasEndCode = false;
        }
    }
};

void ReadTexel(inout EndCodeCounter endCodeCounter, inout TextureStepper uStepper, uint v, uint charSizeH, TexturedLineParams lineParams, out uint color, out bool transparent) {
    const uint u = uStepper.Value();

    const uint charIndex = u + v * charSizeH;
    // Read next texel
    switch (lineParams.mode_color.colorMode) {
        case 0: // 4 bpp, 16 colors, bank mode
            color = ReadVRAM8(lineParams.srca_size.charAddress + (charIndex >> 1));
            color = (color >> ((~u & 1) * 4)) & 0xF;
            endCodeCounter.ProcessEndCode(color == 0xF);
            transparent = color == 0x0;
            color |= lineParams.mode_color.color & 0xFFF0;
            break;
        case 1: // 4 bpp, 16 colors, lookup table mode
            color = ReadVRAM8(lineParams.srca_size.charAddress + (charIndex >> 1));
            color = (color >> ((~u & 1) * 4)) & 0xF;
            endCodeCounter.ProcessEndCode(color == 0xF);
            transparent = color == 0x0;
            color = ReadVRAM16(color * 2 + lineParams.mode_color.color * 8);
            break;
        case 2: // 8 bpp, 64 colors, bank mode
            color = ReadVRAM8(lineParams.srca_size.charAddress + charIndex);
            endCodeCounter.ProcessEndCode(color == 0xFF);
            transparent = color == 0x00;
            color &= 0x3F;
            color |= lineParams.mode_color.color & 0xFFC0;
            break;
        case 3: // 8 bpp, 128 colors, bank mode
            color = ReadVRAM8(lineParams.srca_size.charAddress + charIndex);
            endCodeCounter.ProcessEndCode(color == 0xFF);
            transparent = color == 0x00;
            color &= 0x7F;
            color |= lineParams.mode_color.color & 0xFF80;
            break;
        case 4: // 8 bpp, 256 colors, bank mode
            color = ReadVRAM8(lineParams.srca_size.charAddress + charIndex);
            endCodeCounter.ProcessEndCode(color == 0xFF);
            transparent = color == 0x00;
            color |= lineParams.mode_color.color & 0xFF00;
            break;
        case 5: // 16 bpp, 32768 colors, RGB mode
            color = ReadVRAM16(lineParams.srca_size.charAddress + charIndex * 2);
            endCodeCounter.ProcessEndCode(color == 0x7FFF);
            transparent = !BitTest(color, 15);
            break;
    }
}

bool PlotTexturedLine(PolyParams poly, int2 coord1, int2 coord2, TexturedLineParams lineParams, inout GouraudStepper gouraudL, inout GouraudStepper gouraudR) {
    if (IsLineSystemClipped(poly, coord1, coord2)) {
        return false;
    }

    const uint charSizeH = lineParams.srca_size.charSize.x;
    if (lineParams.mode_color.colorMode == 5) {
        // Force-align character address in 16 bpp RGB mode
        lineParams.srca_size.charAddress &= ~0xF;
    }

    const int2 sysClip = int2(
        BitExtract(poly.sysClip, 0, 16),
        BitExtract(poly.sysClip, 16, 16)
    );

    const uint v = lineParams.texVStepper.Value();

    LineStepper lineStepper = NewLineStepper(coord1, coord2, true);
    const uint skipSteps = lineStepper.SystemClip(sysClip);
    
    PixelParams pixelParams;
    pixelParams.mode_color = lineParams.mode_color;
    if (lineParams.mode_color.gouraudEnable) {
        pixelParams.gouraud.Setup(lineStepper.Length() + 1, gouraudL.Value(), gouraudR.Value());
        pixelParams.gouraud.Skip(skipSteps);
    }
    
    int uStart = 0;
    int uEnd = charSizeH - 1;
    const bool flipH = BitTest(lineParams.control, 4);
    if (flipH) {
        int tmp = uStart;
        uStart = uEnd;
        uEnd = tmp;
    }
    const bool useHighSpeedShrink = lineParams.mode_color.highSpeedShrink && lineStepper.Length() < charSizeH - 1;
    const bool evenOddCoordSelect = BitTest(config.params, 6);
    const bool userClippingEnable = lineParams.mode_color.userClippingEnable;
    const bool clippingMode = lineParams.mode_color.clippingMode;
    const bool transparentPixelDisable = lineParams.mode_color.transparentPixelDisable;

    TextureStepper uStepper;
    uStepper.Setup(lineStepper.Length() + 1, uStart, uEnd, useHighSpeedShrink, evenOddCoordSelect);
    uStepper.SkipPixels(skipSteps);
    
    uint color = 0;
    bool transparent = true;
    EndCodeCounter endCodeCounter;
    endCodeCounter.hasEndCode = false;
    endCodeCounter.count = useHighSpeedShrink ? -2147483648 : 0;
    endCodeCounter.enable = !lineParams.mode_color.endCodeDisable;
    
    // TODO: convert readTexel lambda to a function
    
    ReadTexel(endCodeCounter, uStepper, v, charSizeH, lineParams, color, transparent);

    bool aa = false;
    bool plotted = false;
    for (lineStepper.Step(); lineStepper.CanStep(); aa = lineStepper.Step()) {
        // Load new texels if U coordinate changed
        while (uStepper.ShouldStepTexel()) {
            uStepper.StepTexel();
            ReadTexel(endCodeCounter, uStepper, v, charSizeH, lineParams, color, transparent);

            if (endCodeCounter.count == 2) {
                break;
            }
        }
        if (endCodeCounter.count == 2) {
            break;
        }
        uStepper.StepPixel();
  
        if (endCodeCounter.hasEndCode || (transparent && !transparentPixelDisable)) {
            // Check if the transparent pixel is in-bounds
            if (!IsPixelClipped(poly, lineStepper.Coord(), userClippingEnable, clippingMode)) {
                plotted = true;
                continue;
            }
            if (aa && !IsPixelClipped(poly, lineStepper.Coord(), userClippingEnable, clippingMode)) {
                plotted = true;
                continue;
            }

            // At this point the pixel is clipped. Bail out if there have been in-bounds pixels before, as no more
            // pixels can be drawn past this point.
            if (plotted) {
                break;
            }

            // Otherwise, continue to the next pixel
            continue;
        }

        pixelParams.mode_color.color = color;

        bool plottedPixel = PlotPixel(poly, lineStepper.Coord(), pixelParams);
        if (aa) {
            if (PlotPixel(poly, lineStepper.AACoord(), pixelParams)) {
                plottedPixel = true;
            }
        }
        if (plottedPixel) {
            plotted = true;
        } else if (plotted) {
            // No more pixels can be drawn past this point
            break;
        }

        if (lineParams.mode_color.gouraudEnable) {
            pixelParams.gouraud.Step();
        }
    }

    if (endCodeCounter.count == 2 && !plotted) {
        // Check that the line is indeed entirely out of bounds.
        // End codes cut the line short, so if it happens to cut the line before it managed to plot a pixel in-bounds,
        // the optimization could interrupt rendering the rest of the quad.
        for (; lineStepper.CanStep(); aa = lineStepper.Step()) {
            if (!IsPixelClipped(poly, lineStepper.Coord(), userClippingEnable, clippingMode)) {
                plotted = true;
                break;
            }
            if (aa && !IsPixelClipped(poly, lineStepper.Coord(), userClippingEnable, clippingMode)) {
                plotted = true;
                break;
            }
        }
    }

    return plotted;
}

void PlotTexturedQuad(PolyParams poly, uint cmdctrl, CMDSRCA_SIZE srca_size, int2 coordA, int2 coordB, int2 coordC, int2 coordD) {
    if (IsQuadSystemClipped(poly, coordA, coordB, coordC, coordD)) {
        return;
    }
    
    const CMDPMOD_COLR pmod_colr = FetchCMDPMOD_COLR(poly.cmdAddress);
    const uint charAddress = srca_size.charAddress;
    const uint2 charSize = srca_size.charSize;
 
    TexturedLineParams lineParams;
    lineParams.control = cmdctrl;
    lineParams.mode_color = pmod_colr;
    lineParams.srca_size = srca_size;
    
    QuadStepper quad = NewQuadStepper(coordA, coordB, coordC, coordD);

    if (pmod_colr.gouraudEnable) {
        const uint gouraudTable = FetchCMDGRDA(poly.cmdAddress);
        
        const Color555 colorA = Uint16ToColor555(ReadVRAM16(gouraudTable + 0));
        const Color555 colorB = Uint16ToColor555(ReadVRAM16(gouraudTable + 2));
        const Color555 colorC = Uint16ToColor555(ReadVRAM16(gouraudTable + 4));
        const Color555 colorD = Uint16ToColor555(ReadVRAM16(gouraudTable + 6));
        
        quad.SetupGouraud(colorA, colorB, colorC, colorD);
    }

    const bool flipV = BitTest(cmdctrl, 5);
    quad.SetupTexture(lineParams.texVStepper, charSize.y, flipV);
    
    // Optimization for the case where the quad goes outside the system clipping area.
    // Skip rendering the rest of the quad when a line is clipped after plotting at least one line.
    // The first few lines of the quad could also be clipped; that is accounted for by requiring at least one
    // plotted line. The point is to skip the calculations once the quad iterator reaches a point where no more lines
    // can be plotted because they all sit outside the system clip area.
    bool plottedLine = false;
    
    // Interpolate linearly over edges A-D and B-C
    for (; quad.CanStep(); quad.Step()) {
        // Plot lines between the interpolated points
        const int2 coordL = quad.edgeL.Coord();
        const int2 coordR = quad.edgeR.Coord();
        while (lineParams.texVStepper.ShouldStepTexel()) {
            lineParams.texVStepper.StepTexel();
        }
        lineParams.texVStepper.StepPixel();
        if (PlotTexturedLine(poly, coordL, coordR, lineParams, quad.edgeL.gouraud, quad.edgeR.gouraud)) {
            plottedLine = true;
        } else if (plottedLine) {
            // No more lines can be drawn past this point
            break;
        }
    }
}

void DrawNormalSprite(uint index, const PolyParams poly, const uint cmdctrl) {
    const uint2 localCoord = Extract16PairSX(poly.localCoord, 13);

    const CMDSRCA_SIZE srca_size = FetchCMDSRCA_SIZE(poly.cmdAddress);
    const uint2 charSize = srca_size.charSize;

    const int2 coordTL = FetchCMDXA_YA(poly.cmdAddress) + localCoord;
    const int2 coordBR = coordTL + charSize - 1;
    
    const int2 coordA = int2(coordTL.x, coordTL.y);
    const int2 coordB = int2(coordBR.x, coordTL.y);
    const int2 coordC = int2(coordBR.x, coordBR.y);
    const int2 coordD = int2(coordTL.x, coordBR.y);

    PlotTexturedQuad(poly, cmdctrl, srca_size, coordA, coordB, coordC, coordD);
}

void DrawScaledSprite(uint index, const PolyParams poly, const uint cmdctrl) {
    const uint2 localCoord = Extract16PairSX(poly.localCoord, 13);

    // TODO: load and parse parameters
    
    const uint2 atlasPos = Extract16PairU(poly.atlasPos);
    const uint2 atlasEnd = atlasPos + Extract16PairU(poly.size);
    
    // TODO: actually render the polygon

    /*for (uint y = atlasPos.y; y < atlasEnd.y; y++) {
        const uint basePos = y * kAtlasStride;
        for (uint x = atlasPos.x; x < atlasEnd.x; x++) {
            const uint pos = (basePos + x) * 4;
            const uint value = ~(x - atlasPos.x + (y - atlasPos.y) * 256);
            polyAtlas.Store(pos, value);
        }
    }*/
}

void DrawDistortedSprite(uint index, const PolyParams poly, const uint cmdctrl) {
    const uint2 localCoord = Extract16PairSX(poly.localCoord, 13);
    
    const int2 coordA = FetchCMDXA_YA(poly.cmdAddress) + localCoord;
    const int2 coordB = FetchCMDXB_YB(poly.cmdAddress) + localCoord;
    const int2 coordC = FetchCMDXC_YC(poly.cmdAddress) + localCoord;
    const int2 coordD = FetchCMDXD_YD(poly.cmdAddress) + localCoord;

    const uint2 atlasPos = Extract16PairU(poly.atlasPos);
    const uint2 atlasEnd = atlasPos + Extract16PairU(poly.size);
    
    // TODO: actually render the polygon

    /*for (uint y = atlasPos.y; y < atlasEnd.y; y++) {
        const uint basePos = y * kAtlasStride;
        for (uint x = atlasPos.x; x < atlasEnd.x; x++) {
            const uint pos = (basePos + x) * 4;
            const uint value = ~(x - atlasPos.x + (y - atlasPos.y) * 256);
            polyAtlas.Store(pos, value);
        }
    }*/
}

void DrawPolygon(uint index, const PolyParams poly) {
    const uint2 localCoord = Extract16PairSX(poly.localCoord, 13);
    
    LineParams lineParams;
    lineParams.mode_color = FetchCMDPMOD_COLR(poly.cmdAddress);
    
    const int2 coordA = FetchCMDXA_YA(poly.cmdAddress) + localCoord;
    const int2 coordB = FetchCMDXB_YB(poly.cmdAddress) + localCoord;
    const int2 coordC = FetchCMDXC_YC(poly.cmdAddress) + localCoord;
    const int2 coordD = FetchCMDXD_YD(poly.cmdAddress) + localCoord;

    if (IsQuadSystemClipped(poly, coordA, coordB, coordC, coordD)) {
        return;
    }
    
    QuadStepper quad = NewQuadStepper(coordA, coordB, coordC, coordD);
    
    if (lineParams.mode_color.gouraudEnable) {
        const uint gouraudTable = FetchCMDGRDA(poly.cmdAddress);
        
        const Color555 colorA = Uint16ToColor555(ReadVRAM16(gouraudTable + 0));
        const Color555 colorB = Uint16ToColor555(ReadVRAM16(gouraudTable + 2));
        const Color555 colorC = Uint16ToColor555(ReadVRAM16(gouraudTable + 4));
        const Color555 colorD = Uint16ToColor555(ReadVRAM16(gouraudTable + 6));
        
        quad.SetupGouraud(colorA, colorB, colorC, colorD);
    }
  
    // Optimization for the case where the quad goes outside the system clipping area.
    // Skip rendering the rest of the quad when a line is clipped after plotting at least one line.
    // The first few lines of the quad could also be clipped; that is accounted for by requiring at least one
    // plotted line. The point is to skip the calculations once the quad iterator reaches a point where no more lines
    // can be plotted because they all sit outside the system clip area.
    bool plottedLine = false;
  
    // Interpolate linearly over edges A-D and B-C
    for (; quad.CanStep(); quad.Step()) {
        const int2 coordL = quad.edgeL.Coord();
        const int2 coordR = quad.edgeR.Coord();

        // Plot lines between the interpolated points
        if (lineParams.mode_color.gouraudEnable) {
            lineParams.gouraudLeft = quad.edgeL.GouraudValue();
            lineParams.gouraudRight = quad.edgeR.GouraudValue();
        }
        if (PlotLine(poly, coordL, coordR, lineParams, true)) {
            plottedLine = true;
        } else if (plottedLine) {
            // No more lines can be drawn past this point
            break;
        }
    }
}

void DrawPolylines(uint index, const PolyParams poly) {
    const uint2 localCoord = Extract16PairSX(poly.localCoord, 13);
    
    LineParams lineParams;
    lineParams.mode_color = FetchCMDPMOD_COLR(poly.cmdAddress);
    
    const int2 coordA = FetchCMDXA_YA(poly.cmdAddress) + localCoord;
    const int2 coordB = FetchCMDXB_YB(poly.cmdAddress) + localCoord;
    const int2 coordC = FetchCMDXC_YC(poly.cmdAddress) + localCoord;
    const int2 coordD = FetchCMDXD_YD(poly.cmdAddress) + localCoord;

    if (IsQuadSystemClipped(poly, coordA, coordB, coordC, coordD)) {
        return;
    }

    Color555 colorA, colorB, colorC, colorD;
    if (lineParams.mode_color.gouraudEnable) {
        const uint gouraudTable = FetchCMDGRDA(poly.cmdAddress);
        colorA = Uint16ToColor555(ReadVRAM16(gouraudTable + 0));
        colorB = Uint16ToColor555(ReadVRAM16(gouraudTable + 2));
        colorC = Uint16ToColor555(ReadVRAM16(gouraudTable + 4));
        colorD = Uint16ToColor555(ReadVRAM16(gouraudTable + 6));
    }
  
    if (lineParams.mode_color.gouraudEnable) {
        lineParams.gouraudLeft = colorA;
        lineParams.gouraudRight = colorB;
    }
    PlotLine(poly, coordA, coordB, lineParams, false);
    if (lineParams.mode_color.gouraudEnable) {
        lineParams.gouraudLeft = colorB;
        lineParams.gouraudRight = colorC;
    }
    PlotLine(poly, coordB, coordC, lineParams, false);
    if (lineParams.mode_color.gouraudEnable) {
        lineParams.gouraudLeft = colorC;
        lineParams.gouraudRight = colorD;
    }
    PlotLine(poly, coordC, coordD, lineParams, false);
    if (lineParams.mode_color.gouraudEnable) {
        lineParams.gouraudLeft = colorD;
        lineParams.gouraudRight = colorA;
    }
    PlotLine(poly, coordD, coordA, lineParams, false);
}

void DrawLine(uint index, const PolyParams poly) {
    const uint2 localCoord = Extract16PairSX(poly.localCoord, 13);
    
    LineParams lineParams;
    lineParams.mode_color = FetchCMDPMOD_COLR(poly.cmdAddress);
    
    const int2 coordA = FetchCMDXA_YA(poly.cmdAddress) + localCoord;
    const int2 coordB = FetchCMDXB_YB(poly.cmdAddress) + localCoord;

    if (lineParams.mode_color.gouraudEnable) {
        const uint gouraudTable = FetchCMDGRDA(poly.cmdAddress);
        lineParams.gouraudLeft = Uint16ToColor555(ReadVRAM16(gouraudTable + 0));
        lineParams.gouraudRight = Uint16ToColor555(ReadVRAM16(gouraudTable + 2));
    }
    
    PlotLine(poly, coordA, coordB, lineParams, false);
}

void Draw(uint index) {
    const PolyParams poly = polyParams[index];

    const uint cmdctrl = FetchCMDCTRL(poly.cmdAddress);
    const uint command = BitExtract(cmdctrl, 0, 4);
    
    switch (command) {
        case kCommandDrawNormalSprite:
            DrawNormalSprite(index, poly, cmdctrl);
            break;
        case kCommandDrawScaledSprite:
            DrawScaledSprite(index, poly, cmdctrl);
            break;
        case kCommandDrawDistortedSprite:
            DrawDistortedSprite(index, poly, cmdctrl);
            break;
        case kCommandDrawDistortedSpriteAlt:
            DrawDistortedSprite(index, poly, cmdctrl);
            break;
        case kCommandDrawPolygon:
            DrawPolygon(index, poly);
            break;
        case kCommandDrawPolylines:
            DrawPolylines(index, poly);
            break;
        case kCommandDrawPolylinesAlt:
            DrawPolylines(index, poly);
            break;
        case kCommandDrawLine:
            DrawLine(index, poly);
            break;
    }

    // polyAtlas.Store(index * 16, poly.atlasPos);
    // polyAtlas.Store(index * 16 + 4, poly.cmdAddress);
    // polyAtlas.Store(index * 16 + 8, cmdctrl);
    // polyAtlas.Store(index * 16 + 12, 0xDEADBEEF);
}

// -----------------------------------------------------------------------------

[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    // TODO: figure out if it is possible to efficiently parallelize individual polygon rendering
    Draw(id.z);
}
