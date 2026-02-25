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

ByteAddressBuffer polyIn : register(t0);
StructuredBuffer<PolyParams> polyParams : register(t1);

RWByteAddressBuffer fbOut : register(u0);

// -----------------------------------------------------------------------------

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
        SignExtend(value32, bits),
        SignExtend((value32 >> 16), bits)
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

uint ReadFB8(uint address) {
    const uint value = fbOut.Load(address & ~3);
    return (value >> ((address & 3) * 8)) & 0xFF;
}

uint ReadFB16(uint address) {
    const uint value = fbOut.Load(address & ~3);
    return (value >> ((address & 2) * 8)) & 0xFFFF;
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
    data = (data & 0xFFFF) << shift;

    address &= ~3;
    uint dummy;
    fbOut.InterlockedAnd(address, mask, dummy);
    fbOut.InterlockedOr(address, data, dummy);
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
static const bool transparentMeshes = false;

void MergePolys(uint2 pos) {
    for (uint i = 0; i < config.numPolys; i++) {
        const PolyParams poly = polyParams[i];

        const int2 fbPos = Extract16PairS(poly.fbPos);
        const int2 size = Extract16PairS(poly.size);

        // Skip out of bounds pixels
        if (any(pos < fbPos) || any(pos >= fbPos + size)) {
            continue;
        }
        
        const int2 relPos = pos - fbPos;
        const int2 atlasPos = Extract16PairS(poly.atlasPos) + relPos;
        
        const uint atlasAddr = (atlasPos.x + atlasPos.y * kAtlasStride) * 4;
        const uint rawValue = polyIn.Load(atlasAddr);
        
        // Skip pixels that haven't been touched
        if (!BitTest(rawValue, kPolyMergerPixelDrawn)) {
            continue;
        }
                
        const uint fbAddr = (pos.x + pos.y * fbSizeH) * 2;
        
        if (rawValue == kPolyMergerSetMSB) {
            const uint bit = 0x80 << ((fbAddr & 3) * 8);
            uint dummy;
            fbOut.InterlockedOr(fbAddr & ~3, bit, dummy);
        } else if (pixel8Bits) {
            const uint value = BitExtract(rawValue, 0, 8);
            const bool meshEnable = BitTest(rawValue, kPolyMergerMesh);
        
            // TODO: what happens if pixelParams.mode.colorCalcBits/gouraudEnable != 0?
       
            if (transparentMeshes && meshEnable) {
                // TODO: write to mesh layer
            } else {
                WriteFB8(fbAddr, value);
                if (transparentMeshes) {
                    // TODO: clear pixel from transparent mesh buffer
                }
            }
        } else {
            // TODO: 8-bit/16-bit mode
            const uint rawColor = BitExtract(rawValue, 0, 16);
            const uint colorCalcBits = BitExtract(rawValue, kPolyMergerColorCalcBitsShift, 2);
            const bool meshEnable = BitTest(rawValue, kPolyMergerMesh);
            
            Color555 srcColor = Uint16ToColor555(rawColor);
            Color555 dstColor = Uint16ToColor555(ReadFB16(fbAddr));

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
            
            if (transparentMeshes && meshEnable) {
                // TODO: write to mesh layer
            } else {
                WriteFB16(fbAddr, value);
                if (transparentMeshes) {
                    // TODO: clear pixel from transparent mesh buffer
                }
            }
        }
    }
}

// -----------------------------------------------------------------------------

[numthreads(32, 32, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    MergePolys(id.xy);
}
