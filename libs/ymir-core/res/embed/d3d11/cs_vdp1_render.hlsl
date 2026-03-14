struct Config {
    uint params;
    uint erase;
    uint eraseWriteValue;
    uint _reserved;
};

struct LineParams {
    int2 coordStart;
    int2 coordEnd;

    uint sysClip;
    uint userClip0;
    uint userClip1;

    //   0-9  Command table entry index
    // 10-15  (reserved)
    //    16  Antialiased
    //    17  Gouraud shading (0=no shading; 1=gouraud)
    //    18  Textured        (0=solid color; 1=textured)
    // 19-23  (reserved)
    // 24-31  Texture V coordinate
    uint params;

    uint gouraud;
};

struct CommandEntry {
    uint ctrl_grda;
    uint pmod_colr;
    uint srca_size;
};

// -----------------------------------------------------------------------------

cbuffer Config : register(b0) {
    Config config;
}

ByteAddressBuffer vram : register(t0);
StructuredBuffer<LineParams> lineParams : register(t1);
StructuredBuffer<CommandEntry> commands : register(t2);
Buffer<uint> lineBins : register(t3);
Buffer<uint> lineBinIndices : register(t4);

RWByteAddressBuffer fbOut : register(u0);

// -----------------------------------------------------------------------------

static const uint2 kVDP1MaxFBSize = uint2(1024, 512);

static const uint kFBSize = 256 * 1024;
static const uint kDeinterlaceFBOffset = 2 * kFBSize;
static const uint kMeshFBOffset = 2 * 2 * kFBSize;

static const uint2 kBinSize = uint2(8, 8);
static const uint2 kBinCount = (kVDP1MaxFBSize + kBinSize - 1) / kBinSize;

// -----------------------------------------------------------------------------

int cross2D(int2 vecA, int2 vecB) {
    return vecA.x * vecB.y - vecA.y * vecB.x;
}

int square(int value) {
    return value * value;
}

int SignedPointToLineDistance(int2 pointCoord, int2 lineCoord1, int2 lineCoord2) {
    const int2 l21 = lineCoord2 - lineCoord1;
    const int2 l1p = lineCoord1 - pointCoord;
    return cross2D(l21, l1p) / sqrt(square(l21.x) + square(l21.y));
}

bool BitTest(uint value, uint bit) {
    return ((value >> bit) & 1) != 0;
}

uint BitExtract(uint value, uint offset, uint length) {
    const uint mask = (1u << length) - 1u;
    return (value >> offset) & mask;
}

uint2 Extract16PairU(uint value32) {
    return uint2(
        BitExtract(value32, 0, 16),
        BitExtract(value32, 16, 16)
    );
}

uint ByteSwap16(uint val) {
    return ((val >> 8) & 0x00FF) |
           ((val << 8) & 0xFF00);
}

uint ReadVRAM8(uint address) {
    return BitExtract(vram.Load(address & ~3), (address & 3) * 8, 8);
}

uint ReadVRAM16(uint address) {
    return ByteSwap16(BitExtract(vram.Load(address & ~3), (address & 2) * 8, 16));
}

uint ReadFB8(uint address) {
    const uint value = fbOut.Load(address & ~3);
    return (value >> ((address & 3) * 8)) & 0xFF;
}

uint ReadFB16(uint address) {
    const uint value = fbOut.Load(address & ~3);
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

#ifndef YMIR_BYPASS_ENHANCEMENTS
uint ReadMesh8(uint address) {
    return ReadFB8(kMeshFBOffset + address);
}

uint ReadMesh16(uint address) {
    return ReadFB16(kMeshFBOffset + address);
}

void WriteMesh8(uint address, uint data) {
    WriteFB8(kMeshFBOffset + address, data);
}

void WriteMesh16(uint address, uint data) {
    WriteFB16(kMeshFBOffset + address, data);
}
#endif

uint4 Uint16ToColor555(uint rawValue) {
    return uint4(
        BitExtract(rawValue, 0, 5),
        BitExtract(rawValue, 5, 5),
        BitExtract(rawValue, 10, 5),
        BitExtract(rawValue, 15, 1)
    );
}

uint Color555ToUint16(uint4 color) {
    return color.r | (color.g << 5) | (color.b << 10) | (color.a << 15);
}

// -----------------------------------------------------------------------------

static const uint fbSizeH = 512 << BitExtract(config.params, 0, 1);
static const uint drawFB = BitExtract(config.params, 7, 1);
static const uint drawFBOffset = drawFB * kFBSize;
static const bool pixel8Bits = BitTest(config.params, 2);
static const bool doubleDensity = BitTest(config.params, 3);
static const bool dblInterlaceEnable = BitTest(config.params, 4);
static const uint dblInterlaceDrawLine = BitExtract(config.params, 5, 1);
#ifdef YMIR_BYPASS_ENHANCEMENTS
static const bool deinterlace = false;
static const bool transparentMeshes = false;
#else
static const bool deinterlace = BitTest(config.params, 29);
static const bool transparentMeshes = BitTest(config.params, 30);
#endif

struct PixelData {
    uint pixel;
#ifndef YMIR_BYPASS_ENHANCEMENTS
    uint mesh;
#endif
};

// Steps over the texels of a texture.
struct TextureStepper {
    int num;
    int den;
    int accum;

    int value;
    int inc;

    int baseAccum;
    int baseValue;

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
        baseAccum = accum;
        baseValue = value;
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

    // Resets the texel counter to the initial value.
    void ResetTexel() {
        value = baseValue;
    }

    // Moves to the pixel at the specified step.
    void SetPixel(uint step) {
        accum = baseAccum + num * step;
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

    int baseValue;
    int baseAccum;

    void Setup(uint length, int start, int end) {
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

        baseValue = value;
        baseAccum = accum;
    }

    void Reset() {
        value = baseValue;
        accum = baseAccum;
    }

    // Skips the specified number of pixels.
    void Skip(int steps) {
        value += intInc * steps;
        accum -= num * steps;
        if (den != 0) {
            while (accum < 0) {
                value += fracInc;
                accum += den;
            }
        }
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
    void Setup(uint length, uint4 gouraudStart, uint4 gouraudEnd) {
        stepperR.Setup(length, gouraudStart.r, gouraudEnd.r);
        stepperG.Setup(length, gouraudStart.g, gouraudEnd.g);
        stepperB.Setup(length, gouraudStart.b, gouraudEnd.b);
    }

    void Reset() {
        stepperR.Reset();
        stepperG.Reset();
        stepperB.Reset();
    }

    // Skips the specified number of pixels.
    void Skip(int steps) {
        if (steps > 0) {
            stepperR.Skip(steps);
            stepperG.Skip(steps);
            stepperB.Skip(steps);
        }
    }

    // Blends the given base color with the current gouraud shading values.
    uint4 Blend(uint4 baseColor) {
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
    int2 start;

    uint dmaj;
    uint step;

    int2 aaInc;

    // Computes how many steps are needed from the start of the line to reach the target pixel.
    // Aligns the major coordinate only.
    uint StepsToTarget(uint2 targetPos, bool antiAlias) {
        const int2 deltaPos = (targetPos - start - (antiAlias ? aaInc : 0)) * majInc;
        const int delta = deltaPos.x + deltaPos.y;

        if (delta < 0 || delta >= int(dmaj) + 1) {
            return dmaj + 1;
        }
        return delta;
    }

    // Sets the slope step to the specified coordinate.
    // Clamped to the length of the line.
    void SetStep(uint targetStep) {
        targetStep = min(targetStep, dmaj);

        const int stepDelta = targetStep + 1 - step;
        if (stepDelta == 0) {
            return;
        }

        step = targetStep + 1;
        pos += majInc * stepDelta;

        // TODO: mask to 13 bits

        accum -= num * stepDelta;
        // NOTE: if stepDelta is ever negative, this will need adjustments.
        // Luckily, the AA pixel is always offset by 0 or +1 from the normal pixel, never -1, and since
        // the normal pixel is rendered before the AA pixel, the accumulator increases monotonically.
        /*if (den != 0) {
            const int count = (accumTarget - accum + den) / den;
            accum += den * count;
            pos += minInc * count;
        }*/
        while (accum <= accumTarget) {
            accum += den;
            pos += minInc;
        }
    }

    // Determines if the current step needs antialiasing.
    bool NeedsAA() {
        return step > 1 && accum - den + num > accumTarget;
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
    stepper.start = coord1;

    int2 delta = coord2 - coord1;
    int2 absDelta = abs(delta); // component-wise
    stepper.dmaj = max(absDelta.x, absDelta.y);
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
    // static const int kShift = 32 - 13;
    //
    // stepper.num <<= kShift;
    // stepper.den <<= kShift;
    // stepper.accum <<= kShift;
    // stepper.accumTarget <<= kShift;

    return stepper;
}

// -----------------------------------------------------------------------------

bool IsPixelUserClipped(const LineParams vdp1line, uint2 coord) {
    const uint2 userClip0 = Extract16PairU(vdp1line.userClip0);
    const uint2 userClip1 = Extract16PairU(vdp1line.userClip1);
    return any(coord < userClip0) || any(coord > userClip1);
}

bool IsPixelSystemClipped(const LineParams vdp1line, uint2 coord) {
    const uint2 sysClip = Extract16PairU(vdp1line.sysClip);
    return any(coord < 0) || any(coord > sysClip);
}

bool IsPixelClipped(const LineParams vdp1line, uint2 coord, bool userClippingEnable, bool clippingMode) {
    if (IsPixelSystemClipped(vdp1line, coord)) {
        return true;
    }
    if (userClippingEnable) {
        // clippingMode = false -> draw inside, reject outside
        // clippingMode = true -> draw outside, reject inside
        // The function returns true if the pixel is clipped, therefore we want to reject pixels that return the
        // opposite of clippingMode on that function.
        if (IsPixelUserClipped(vdp1line, coord) != clippingMode) {
            return true;
        }
    }
    return false;
}

void PlotPixel(uint2 coord, inout PixelData pixelData, const uint cmdModeColor, const GouraudStepper gouraudStepper) {
    const bool gouraudEnable = BitTest(cmdModeColor, 2);
    const bool meshEnable = BitTest(cmdModeColor, 8);
    const bool msbOn = BitTest(cmdModeColor, 15);

    if (!transparentMeshes && meshEnable && BitTest(coord.x ^ coord.y, 0)) {
        return;
    }

    // TODO: preClippingDisable

    if (msbOn) {
        uint bit = 0x8000;
        if (pixel8Bits && !BitTest(coord.x, 0)) {
            bit >>= 8;
        }
        pixelData.pixel |= bit;
    } else {
        uint value;
        if (pixel8Bits) {
            const uint value = BitExtract(cmdModeColor, 16, 8);

            // TODO: what happens if pixelParams.mode.colorCalcBits/gouraudEnable != 0?

#ifndef YMIR_BYPASS_ENHANCEMENTS
            if (transparentMeshes && meshEnable) {
                pixelData.mesh = value;
            } else {
#endif
                pixelData.pixel = value;
#ifndef YMIR_BYPASS_ENHANCEMENTS
                if (transparentMeshes) {
                    pixelData.mesh = 0;
                }
            }
#endif
        } else {
            const uint rawColor = BitExtract(cmdModeColor, 16, 16);
            const uint colorCalcBits = BitExtract(cmdModeColor, 0, 2);

            uint4 srcColor = Uint16ToColor555(rawColor);
            uint4 dstColor;

            if (gouraudEnable) {
                // Apply gouraud shading to source color
                srcColor = gouraudStepper.Blend(srcColor);
            }

            // Apply color calculations
            //
            // In all cases where calculation is done, the raw color data to be drawn ("original graphic") or from
            // the background are interpreted as 5:5:5 RGB.

            switch (colorCalcBits) {
                case 0: // Replace
                    dstColor = srcColor;
                    break;
                case 1: // Shadow
                    // Halve destination luminosity if it's not transparent
                    dstColor = Uint16ToColor555(pixelData.pixel);
                    if (dstColor.a) {
                        dstColor.r >>= 1u;
                        dstColor.g >>= 1u;
                        dstColor.b >>= 1u;
                    }
                    break;
                case 2: // Half-luminance
                    // Draw original graphic with halved luminance
                    dstColor.r = srcColor.r >> 1u;
                    dstColor.g = srcColor.g >> 1u;
                    dstColor.b = srcColor.b >> 1u;
                    dstColor.a = srcColor.a;
                    break;
                case 3: // Half-transparency
                    // If background is not transparent, blend half of original graphic and half of background
                    // Otherwise, draw original graphic as is
                    dstColor = Uint16ToColor555(pixelData.pixel);
                    if (dstColor.a) {
                        dstColor.r = (srcColor.r + dstColor.r) >> 1u;
                        dstColor.g = (srcColor.g + dstColor.g) >> 1u;
                        dstColor.b = (srcColor.b + dstColor.b) >> 1u;
                    } else {
                        dstColor = srcColor;
                    }
                    break;
            }

            const uint value = Color555ToUint16(dstColor);

#ifndef YMIR_BYPASS_ENHANCEMENTS
            if (transparentMeshes && meshEnable) {
                pixelData.mesh = value;
            } else {
#endif
                pixelData.pixel = value;
#ifndef YMIR_BYPASS_ENHANCEMENTS
                if (transparentMeshes) {
                    pixelData.mesh = 0;
                }
            }
#endif
        }
    }
}

void ReadTexel(uint u, uint v, uint charAddress, uint charSizeH, uint colorMode, uint colorData, out uint color, out bool transparent, out bool hasEndCode) {
    const uint charIndex = u + v * charSizeH;

    switch (colorMode) {
        case 0: // 4 bpp, 16 colors, bank mode
            color = ReadVRAM8(charAddress + (charIndex >> 1));
            color = (color >> ((~u & 1) * 4)) & 0xF;
            hasEndCode = color == 0xF;
            transparent = color == 0x0;
            color |= colorData & 0xFFF0;
            break;
        case 1: // 4 bpp, 16 colors, lookup table mode
            color = ReadVRAM8(charAddress + (charIndex >> 1));
            color = (color >> ((~u & 1) * 4)) & 0xF;
            hasEndCode = color == 0xF;
            transparent = color == 0x0;
            color = ReadVRAM16(color * 2 + colorData * 8);
            break;
        case 2: // 8 bpp, 64 colors, bank mode
            color = ReadVRAM8(charAddress + charIndex);
            transparent = color == 0x00;
            hasEndCode = color == 0xFF;
            color &= 0x3F;
            color |= colorData & 0xFFC0;
            break;
        case 3: // 8 bpp, 128 colors, bank mode
            color = ReadVRAM8(charAddress + charIndex);
            transparent = color == 0x00;
            hasEndCode = color == 0xFF;
            color &= 0x7F;
            color |= colorData & 0xFF80;
            break;
        case 4: // 8 bpp, 256 colors, bank mode
            color = ReadVRAM8(charAddress + charIndex);
            transparent = color == 0x00;
            hasEndCode = color == 0xFF;
            color |= colorData & 0xFF00;
            break;
        case 5: // 16 bpp, 32768 colors, RGB mode
            color = ReadVRAM16((charAddress & ~0xF) + charIndex * 2);
            transparent = !BitTest(color, 15);
            hasEndCode = color == 0x7FFF;
            break;
    }
}

uint FindEndCode(uint charAddress, uint uStart, uint uEnd, uint texV, uint charSizeH, uint colorMode) {
    const uint baseCharIndex = texV * charSizeH;

    uint endCodeCount = 0;

    int uInc = uStart <= uEnd ? +1 : -1;
    for (int u = uStart; u != int(uEnd); u += uInc) {
        const uint charIndex = u + baseCharIndex;

        uint color;
        switch (colorMode) {
            case 0: // 4 bpp, 16 colors, bank mode
                color = ReadVRAM8(charAddress + (charIndex >> 1));
                color = (color >> ((~u & 1) * 4)) & 0xF;
                if (color == 0xF) {
                    endCodeCount++;
                }
                break;
            case 1: // 4 bpp, 16 colors, lookup table mode
                color = ReadVRAM8(charAddress + (charIndex >> 1));
                color = (color >> ((~u & 1) * 4)) & 0xF;
                if (color == 0xF) {
                    endCodeCount++;
                }
                break;
            case 2: // 8 bpp, 64 colors, bank mode
                color = ReadVRAM8(charAddress + charIndex);
                if (color == 0xFF) {
                    endCodeCount++;
                }
                break;
            case 3: // 8 bpp, 128 colors, bank mode
                color = ReadVRAM8(charAddress + charIndex);
                if (color == 0xFF) {
                    endCodeCount++;
                }
                break;
            case 4: // 8 bpp, 256 colors, bank mode
                color = ReadVRAM8(charAddress + charIndex);
                if (color == 0xFF) {
                    endCodeCount++;
                }
                break;
            case 5: // 16 bpp, 32768 colors, RGB mode
                color = ReadVRAM16((charAddress & ~0xF) + charIndex * 2);
                if (color == 0x7FFF) {
                    endCodeCount++;
                }
                break;
        }
        if (endCodeCount == 2) {
            return u;
        }
    }

    return charSizeH;
}

void DrawLine(uint2 pos, uint lineIndex, inout PixelData pixelData) {
    LineParams vdp1line = lineParams[lineIndex];
    const uint cmdIndex = BitExtract(vdp1line.params, 0, 10);

    const uint cmdModeColor = commands[cmdIndex].pmod_colr;
    const bool userClippingEnable = BitTest(cmdModeColor, 10);
    const bool clippingMode = BitTest(cmdModeColor, 9);

    if (IsPixelClipped(vdp1line, pos, userClippingEnable, clippingMode)) {
        return;
    }

    const bool antiAlias = BitTest(vdp1line.params, 16);
    const bool gouraudEnable = BitTest(vdp1line.params, 17);
    const bool textured = BitTest(vdp1line.params, 18);
    const uint texV = BitExtract(vdp1line.params, 24, 8);

    const uint4 gouraudStart = Uint16ToColor555(BitExtract(vdp1line.gouraud, 0, 16));
    const uint4 gouraudEnd = Uint16ToColor555(BitExtract(vdp1line.gouraud, 16, 16));

    LineStepper lineStepper = NewLineStepper(vdp1line.coordStart, vdp1line.coordEnd, antiAlias);

    GouraudStepper gouraudStepper;
    bool gouraudInit = true;

    TextureStepper uStepper;
    uint charAddress;
    uint charSizeH;
    bool flipH;
    uint colorMode;
    uint colorData;
    bool transparentPixelDisable;
    uint endCodeIndex;
    bool endCodesEnabled;
    bool checkEndCodes;

    if (textured) {
        charSizeH = max(BitExtract(commands[cmdIndex].srca_size, 8 + 16, 6) << 3, 1);
        flipH = BitTest(commands[cmdIndex].ctrl_grda, 4);
        charAddress = BitExtract(commands[cmdIndex].srca_size, 0, 16) << 3;
        colorMode = BitExtract(cmdModeColor, 3, 3);
        colorData = BitExtract(cmdModeColor, 16, 16);
        transparentPixelDisable = BitTest(cmdModeColor, 6);
        const bool useHighSpeedShrink = BitTest(cmdModeColor, 12) && lineStepper.Length() < charSizeH - 1;
        const bool evenOddCoordSelect = BitTest(config.params, 6);
        endCodesEnabled = !BitTest(cmdModeColor, 7);

        int uStart = 0;
        int uEnd = charSizeH - 1;
        if (flipH) {
            int tmp = uStart;
            uStart = uEnd;
            uEnd = tmp;
        }

        uStepper.Setup(lineStepper.Length() + 1, uStart, uEnd, useHighSpeedShrink, evenOddCoordSelect);

        if (endCodesEnabled && !useHighSpeedShrink) {
            endCodeIndex = FindEndCode(charAddress, uStart, uEnd, texV, charSizeH, colorMode);
            checkEndCodes = endCodeIndex < charSizeH;
        } else {
            checkEndCodes = false;
        }
    }

    const uint steps = lineStepper.StepsToTarget(pos, false);
    if (steps <= lineStepper.Length()) {
        lineStepper.SetStep(steps);

        if (all(lineStepper.Coord() == int2(pos))) {
            if (gouraudEnable) {
                if (gouraudInit) {
                    gouraudInit = false;
                    gouraudStepper.Setup(lineStepper.Length() + 1, gouraudStart, gouraudEnd);
                } else {
                    gouraudStepper.Reset();
                }
                gouraudStepper.Skip(steps);
            }

            if (textured) {
                // TODO: optimize this
                uStepper.SetPixel(steps);
                uStepper.ResetTexel();
                while (uStepper.ShouldStepTexel()) {
                    uStepper.StepTexel();
                }

                // TODO: simplify this mess
                const uint texU = uStepper.Value();
                if (!checkEndCodes || (flipH ? (texU > endCodeIndex) : (texU < endCodeIndex))) {
                    uint color;
                    bool transparent;
                    bool hasEndCode;
                    ReadTexel(texU, texV, charAddress, charSizeH, colorMode, colorData, color, transparent, hasEndCode);

                    if ((!hasEndCode || !endCodesEnabled) && (!transparent || transparentPixelDisable)) {
                        const uint texModeColor = BitExtract(cmdModeColor, 0, 16) | (color << 16);
                        PlotPixel(pos, pixelData, texModeColor, gouraudStepper);
                    }
                }
            } else {
                PlotPixel(pos, pixelData, cmdModeColor, gouraudStepper);
            }
        }
    }

    // TODO: deduplicate code
    if (antiAlias) {
        const uint steps = lineStepper.StepsToTarget(pos, true);
        if (steps <= lineStepper.Length()) {
            lineStepper.SetStep(steps);

            if (lineStepper.NeedsAA() && all(lineStepper.AACoord() == int2(pos))) {
                if (gouraudEnable) {
                    if (gouraudInit) {
                        gouraudInit = false;
                        gouraudStepper.Setup(lineStepper.Length() + 1, gouraudStart, gouraudEnd);
                    } else {
                        gouraudStepper.Reset();
                    }
                    gouraudStepper.Skip(steps);
                }

                if (textured) {
                    uStepper.SetPixel(steps);
                    uStepper.ResetTexel();

                    while (uStepper.ShouldStepTexel()) {
                        uStepper.StepTexel();
                    }

                    const uint texU = uStepper.Value();
                    if (!checkEndCodes || (flipH ? (texU > endCodeIndex) : (texU < endCodeIndex))) {
                        uint color;
                        bool transparent;
                        bool hasEndCode;
                        ReadTexel(texU, texV, charAddress, charSizeH, colorMode, colorData, color, transparent, hasEndCode);

                        if ((!hasEndCode || !endCodesEnabled) && (!transparent || transparentPixelDisable)) {
                            const uint texModeColor = BitExtract(cmdModeColor, 0, 16) | (color << 16);
                            PlotPixel(pos, pixelData, texModeColor, gouraudStepper);
                        }
                    }
                } else {
                    PlotPixel(pos, pixelData, cmdModeColor, gouraudStepper);
                }
            }
        }
    }
}

[numthreads(kBinSize.x, kBinSize.y, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint2 pos = id.xy;
    uint2 fbPos = pos;
    uint baseFBAddr = 0;
    if (dblInterlaceEnable) {
        if ((pos.y & 1) != dblInterlaceDrawLine) {
            if (!deinterlace) {
                return;
            }
            baseFBAddr = kDeinterlaceFBOffset;
            // TODO: on SH2 writes, copy FBRAM to both buffers
            // TODO: on VDP2 sprite drawing, read from both buffers, alternating every line
        }
        fbPos.y >>= 1;
    }

    const uint fbAddr = baseFBAddr + fbPos.y * fbSizeH + fbPos.x;

    PixelData pixelData;
    if (pixel8Bits) {
        pixelData.pixel = ReadFB8(drawFBOffset + fbAddr);
#ifndef YMIR_BYPASS_ENHANCEMENTS
        if (transparentMeshes) {
            pixelData.mesh = ReadMesh8(drawFBOffset + fbAddr);
        }
#endif
    } else {
        pixelData.pixel = ReadFB16(drawFBOffset + (fbAddr << 1));
#ifndef YMIR_BYPASS_ENHANCEMENTS
        if (transparentMeshes) {
            pixelData.mesh = ReadMesh16(drawFBOffset + (fbAddr << 1));
        }
#endif
    }

    const uint2 binPos = pos / kBinSize;
    const uint binIndex = binPos.y * kBinCount.x + binPos.x;
    const uint binOffset = lineBinIndices[binIndex];
    const uint numLines = lineBinIndices[binIndex + 1] - binOffset;

    for (uint index = 0; index < numLines; index++) {
        const uint lineIndex = lineBins[binOffset + index];
        const int2 lineStart = lineParams[lineIndex].coordStart;
        const int2 lineEnd = lineParams[lineIndex].coordEnd;

        const int2 lowerBound = min(lineStart, lineEnd);
        const int2 upperBound = max(lineStart, lineEnd);

        if (all(int2(pos) < lowerBound) || all(int2(pos) > upperBound)) {
            continue;
        }

        if (abs(SignedPointToLineDistance(int2(pos), lineStart, lineEnd)) > 1) {
            continue;
        }

        DrawLine(pos, lineIndex, pixelData);
    }

    if (pixel8Bits) {
        WriteFB8(drawFBOffset + fbAddr, pixelData.pixel);
#ifndef YMIR_BYPASS_ENHANCEMENTS
        if (transparentMeshes) {
            WriteMesh8(drawFBOffset + fbAddr, pixelData.mesh);
        }
#endif
    } else {
        WriteFB16(drawFBOffset + (fbAddr << 1), pixelData.pixel);
#ifndef YMIR_BYPASS_ENHANCEMENTS
        if (transparentMeshes) {
            WriteMesh16(drawFBOffset + (fbAddr << 1), pixelData.mesh);
        }
#endif
    }
}
