struct Config {
    uint displayParams;
    uint startY;
    uint extraParams;
    uint vcellScrollParams;
    uint2 spriteParams;
    uint windows;
    uint scale;
};

struct ComposeParams {
    uint params;
    int3 colorOffsetA;
    int3 colorOffsetB;
    uint bgColorCalcRatios;
    uint backLineColorCalcRatios;
    uint3 _reserved;
};

// -----------------------------------------------------------------------------

cbuffer Config : register(b0) {
    Config config;
}

Texture2DArray<uint4> bgIn : register(t0);
Texture2DArray<uint4> rbgLineColorIn : register(t1);
Texture2D<uint4> lineColorIn : register(t2);
Texture2D<uint> spriteAttrsIn : register(t3);
StructuredBuffer<ComposeParams> composeParams : register(t4);
Texture2D<uint4> colorCalcWindowIn : register(t5);

RWTexture2D<float4> textureOut : register(u0);

// -----------------------------------------------------------------------------

static const uint kPixelAttrBitSpriteColorMSB = 3;
static const uint kPixelAttrBitSpriteShadowWindow = 4;
static const uint kPixelAttrBitSpriteNormalShadow = 5;
static const uint kPixelAttrBitSpecColorCalc = 6;
static const uint kPixelAttrBitTransparent = 7;

static const uint kBGLayerNBG0 = 0;
static const uint kBGLayerNBG1 = 1;
static const uint kBGLayerNBG2 = 2;
static const uint kBGLayerNBG3 = 3;
static const uint kBGLayerRBG0 = 4;
static const uint kBGLayerRBG1 = 5;
static const uint kBGLayerSprite = 6;
static const uint kBGLayerMesh = 7;
static const uint kBGLayerInvalid = 8;

static const uint kLayerSprite = 0;
static const uint kLayerRBG0 = 1;
static const uint kLayerNBG0_RBG1 = 2;
static const uint kLayerNBG1_EXBG = 3;
static const uint kLayerNBG2 = 4;
static const uint kLayerNBG3 = 5;
static const uint kLayerBack = 6;
static const uint kLayerLine = 7; // not used in the stack, but referenced by parameters
static const uint kLayerMesh = 8; // not used in the stack, but referenced by parameters

static const uint kSpriteCCCondPriorityLE = 0;
static const uint kSpriteCCCondPriorityEQ = 1;
static const uint kSpriteCCCondPriorityGE = 2;
static const uint kSpriteCCCondColorMSB = 3;

static const uint4 kTransparentPixel = uint4(0, 0, 0, 128);
static const uint3 kBlackPixel = uint3(0, 0, 0);

// -----------------------------------------------------------------------------

bool BitTest(uint value, uint bit) {
    return ((value >> bit) & 1) != 0;
}

uint BitExtract(uint value, uint offset, uint length) {
    const uint mask = (1u << length) - 1u;
    return (value >> offset) & mask;
}

static const bool interlaced = BitExtract(config.displayParams, 0, 2) >= 2;
static const uint oddField = BitExtract(config.displayParams, 2, 1);
static const bool exclusiveMonitor = BitTest(config.displayParams, 3);

static const uint kScaleBits = 12;
static const uint kScaleOne = 1 << kScaleBits;

static const bool deinterlace = BitTest(config.extraParams, 28);
static const bool transparentMeshes = BitTest(config.extraParams, 29);
static const uint scale = BitExtract(config.scale, 0, 16);
static const uint scaleStep = BitExtract(config.scale, 16, 16);

uint ScaleUp(uint value) {
    return (value * scale) >> kScaleBits;
}

// The alpha channel of the layer textures contains pixel attributes:
// bits  use
//  0-2  Priority (0 to 7)
//    3  Color MSB (sprite only)
//    4  Sprite shadow/window flag (sprite only; SD = 1)
//    5  Sprite normal shadow flag (sprite only; DC LSB = 0, rest of the bits = 1)
//    6  Special color calculation flag
//    7  Transparent flag (0=opaque, 1=transparent)
struct Attributes {
    uint priority;
    bool specColorCalc;
    bool transparent;
};

Attributes ToAttributes(uint pixelData) {
    Attributes attrs;
    attrs.priority = BitExtract(pixelData, 0, 3);
    attrs.specColorCalc = BitTest(pixelData, kPixelAttrBitSpecColorCalc);
    attrs.transparent = BitTest(pixelData, kPixelAttrBitTransparent);
    return attrs;
}

uint GetOutputY(uint y) {
    if (!deinterlace && interlaced && !exclusiveMonitor) {
        return (y << 1) | oddField;
    } else {
        return y;
    }
}

uint GetLoResInputY(uint y) {
    y = (y * scaleStep) >> kScaleBits;
    if (deinterlace && interlaced) {
        return y >> 1;
    } else {
        return y;
    }
}

// -----------------------------------------------------------------------------

bool IsBGLayerEnabled(uint bgLayer) {
    return BitTest(config.extraParams, bgLayer + 8);
}

bool IsLayerEnabled(uint layer) {
    return BitTest(config.extraParams, layer);
}

uint GetBGLayerIndex(uint layer) {
    switch (layer) {
        case kLayerSprite:
            return kBGLayerSprite;
        case kLayerMesh:
            return kBGLayerMesh;
        case kLayerRBG0:
            return kBGLayerRBG0;
        case kLayerNBG0_RBG1:
            return IsBGLayerEnabled(kBGLayerRBG1) ? kBGLayerRBG1 : kBGLayerNBG0;
        case kLayerNBG1_EXBG:
            return kBGLayerNBG1;
        case kLayerNBG2:
            return kBGLayerNBG2;
        case kLayerNBG3:
            return kBGLayerNBG3;
        default:
            return kBGLayerInvalid; // go out of bounds intentionally to read blanks
    }
}

bool IsColorCalcEnabled(uint layer, uint2 pos) {
    const bool enabled = BitTest(composeParams[0].params, layer);
    if (layer >= kLayerBack) {
        // Back and line screen layers use the enable bit alone
        return enabled;
    }
    if (!enabled) {
        // Color calculation is disabled for this layer
        return false;
    }
    if (colorCalcWindowIn[uint2((pos.x * scaleStep) >> kScaleBits, GetLoResInputY(pos.y))].r != 0) {
        // Inside color calculation window
        return false;
    }
    if (layer == kLayerSprite) {
        // Sprites use condition modes based on priority or color MSB
        const uint attrs = bgIn[uint3(pos.xy, kBGLayerSprite)].a;
        const uint priority = BitExtract(attrs, 0, 3);
        const uint value = BitExtract(config.displayParams, 19, 3);
        const uint cond = BitExtract(config.displayParams, 22, 2);
        switch (cond) {
            case kSpriteCCCondPriorityLE:
                return priority <= value;
            case kSpriteCCCondPriorityEQ:
                return priority == value;
            case kSpriteCCCondPriorityGE:
                return priority >= value;
            case kSpriteCCCondColorMSB:
                return BitTest(attrs, kPixelAttrBitSpriteColorMSB);
        }
        return false;
    }
    // BG layers use the per-pixel special color calculation flag
    const uint bgLayer = GetBGLayerIndex(layer);
    const uint attrs = bgIn[uint3(pos.xy, bgLayer)].a;
    return BitTest(attrs, kPixelAttrBitSpecColorCalc);
}

bool IsLineColorEnabled(uint layer, uint2 pos) {
    return BitTest(composeParams[0].params, layer + 25);
}

uint3 GetLineColor(uint layer, uint2 pos) {
    if (layer == kLayerRBG0 || (layer == kLayerNBG0_RBG1 && IsBGLayerEnabled(kBGLayerRBG1))) {
        return rbgLineColorIn[uint3((pos * scaleStep) >> kScaleBits, layer - kLayerRBG0)].rgb;
    }
    return lineColorIn[uint2(0, GetLoResInputY(pos.y))].rgb;
}

int GetColorCalcRatio(uint layer, uint2 pos) {
    switch (layer) {
        case kLayerSprite:
            return spriteAttrsIn[pos];
        case kLayerRBG0:
        case kLayerNBG0_RBG1:
        case kLayerNBG1_EXBG:
        case kLayerNBG2:
        case kLayerNBG3:
            return BitExtract(composeParams[0].bgColorCalcRatios, (layer - kLayerRBG0) * 5, 5);
        case kLayerBack:
        case kLayerLine:
            return BitExtract(composeParams[0].backLineColorCalcRatios, IsColorCalcEnabled(layer, uint2(pos.x, GetLoResInputY(pos.y))) ? 5 : 0, 5);
        default:
            return 31;
    }
}

bool IsColorOffsetEnabled(uint layer) {
    return BitTest(composeParams[0].params, layer + 11);
}

int3 GetColorOffset(uint layer) {
    const bool selB = BitTest(composeParams[0].params, 18 + layer);
    return selB ? composeParams[0].colorOffsetB : composeParams[0].colorOffsetA;
}

uint4 GetLayerOutput(uint layer, uint2 pos) {
    switch (layer) {
        case kLayerSprite:
        case kLayerMesh:
        case kLayerRBG0:
        case kLayerNBG0_RBG1:
        case kLayerNBG1_EXBG:
        case kLayerNBG2:
        case kLayerNBG3:
            return bgIn[uint3(pos.xy, GetBGLayerIndex(layer))];
        case kLayerBack:
            return lineColorIn[uint2(1, GetLoResInputY(pos.y))]; // the attribute byte doesn't matter
        case kLayerLine:
            return lineColorIn[uint2(0, GetLoResInputY(pos.y))]; // the attribute byte doesn't matter
        default:
            return kTransparentPixel; // should never happpen
    }
}

uint3 Compose(uint2 basePos) {
    const uint2 pos = uint2(basePos.x, GetOutputY(basePos.y));

    // Clear screen if display is disabled
    const bool displayEnabled = BitTest(config.displayParams, 30);
    if (!displayEnabled) {
        const bool borderColorMode = BitTest(config.displayParams, 31);
        if (borderColorMode) {
            // Use back screen color
            return lineColorIn[uint2(1, GetLoResInputY(basePos.y))].rgb;
        }
        return kBlackPixel.rgb;
    }

    uint layerStack[3] = { kLayerBack, kLayerBack, kLayerBack };
    uint layerPrios[3] = { 0, 0, 0 };

    for (uint layer = 0; layer < 6; layer++) {
        // Skip disabled layers
        if (!IsLayerEnabled(layer)) {
            continue;
        }

        const uint4 layerOutput = GetLayerOutput(layer, pos);
        const Attributes attrs = ToAttributes(layerOutput.a);

        // Skip transparent pixels
        if (attrs.transparent) {
            continue;
        }

        // Priority zero also means transparent pixel
        if (attrs.priority == 0) {
            continue;
        }

        // Skip normal shadow sprite layer pixels
        if (layer == kLayerSprite) {
            if (BitTest(layerOutput.a, kPixelAttrBitSpriteNormalShadow)) {
                continue;
            }
        }

        // Insert the layer into the appropriate position in the stack
        // - Higher priority beats lower priority
        // - If same priority, lower Layer index beats higher Layer index
        // - layerStack[0] is topmost (first) layer
        for (int i = 0; i < 3; i++) {
            if (attrs.priority > layerPrios[i] || (attrs.priority == layerPrios[i] && layer < layerStack[i])) {
                // Push layers back
                for (int j = 2; j > i; j--) {
                    layerStack[j] = layerStack[j - 1];
                    layerPrios[j] = layerPrios[j - 1];
                }
                layerStack[i] = layer;
                layerPrios[i] = attrs.priority;
                break;
            }
        }
    }

    // Find sprite mesh layer stack position
    uint meshLayer = 0xFF;
    uint3 meshPixel;
    if (transparentMeshes && IsLayerEnabled(kLayerSprite)) {
        const uint4 meshOutput = GetLayerOutput(kLayerMesh, pos);
        meshPixel = meshOutput.rgb;
        const Attributes meshAttrs = ToAttributes(meshOutput.a);
        if (!meshAttrs.transparent && meshAttrs.priority > 0 && !BitTest(meshOutput.a, kPixelAttrBitSpriteNormalShadow)) {
            for (uint i = 0; i < 3; i++) {
                // The sprite layer has the highest priority on ties, so the priority check can be simplified.
                // Sprite pixels drawn of top of mesh pixels erase the corresponding pixels from the mesh layer,
                // therefore the mesh layer can be considered always on top of the sprite layer.
                if (meshAttrs.priority >= layerPrios[i]) {
                    meshLayer = i;
                    break;
                }
            }
        }
    }

    uint3 output = { 0, 0, 0 };

    const bool layer0LineColorEnabled = IsLineColorEnabled(layerStack[0], pos);
    const bool extendedColorCalc = BitTest(composeParams[0].params, 8);

    uint3 layer0Pixel = GetLayerOutput(layerStack[0], pos).rgb;
    uint3 layer1Pixel = GetLayerOutput(layerStack[1], pos).rgb;

    if (extendedColorCalc) {
        if (IsColorCalcEnabled(layerStack[1], pos)) {
            uint3 layer2Pixel = GetLayerOutput(layerStack[2], pos).rgb;

            // Blend layer 2 with sprite mesh layer colors
            // TODO: apply color calculation effects
            if (transparentMeshes && meshLayer == 2) {
                layer2Pixel = (layer2Pixel + meshPixel) >> 1;
            }

            layer1Pixel = (layer1Pixel + layer2Pixel) >> 1;
        }

        if (layer0LineColorEnabled) {
            const uint3 lineColor = GetLineColor(layerStack[0], basePos);
            if (IsColorCalcEnabled(kLayerLine, pos)) {
                layer1Pixel = (layer1Pixel + lineColor) >> 1;
            } else {
                layer1Pixel = lineColor;
            }
        }
    } else if (layer0LineColorEnabled) {
        layer1Pixel = GetLineColor(layerStack[0], basePos);
    }

    // Blend layer 1 with sprite mesh layer colors
    // TODO: apply color calculation effects
    if (transparentMeshes && meshLayer == 1) {
        layer1Pixel = (layer1Pixel + meshPixel) >> 1;
    }

    if (IsColorCalcEnabled(layerStack[0], pos)) {
        const bool useAdditiveBlend = BitTest(composeParams[0].params, 9);
        if (useAdditiveBlend) {
            output = min(layer0Pixel + layer1Pixel, 255);
        } else {
            const bool useSecondScreenRatio = BitTest(composeParams[0].params, 10);
            const uint ratioLayer = useSecondScreenRatio ? layerStack[1] : layerStack[0];
            const int ratio = GetColorCalcRatio(ratioLayer, pos);
            output = int3(layer1Pixel) + (((int3(layer0Pixel) - int3(layer1Pixel)) * ratio) >> 5);
        }
    } else {
        output = layer0Pixel;
    }

    // Blend layer 0 with sprite mesh layer colors
    // TODO: apply color calculation effects
    if (transparentMeshes && meshLayer == 0) {
        output = (output + meshPixel) >> 1;
    }

    // Apply sprite shadow if sprite layer has a shadow pixel and is on top of the topmost layer
    const uint4 spriteOutput = GetLayerOutput(kLayerSprite, pos);
    const uint spritePriority = BitExtract(spriteOutput.a, 0, 3);
    if (spritePriority >= layerPrios[0]) {
        const bool useSpriteWindow = BitTest(config.displayParams, 17);
        const bool isNormalShadow = BitTest(spriteOutput.a, kPixelAttrBitSpriteNormalShadow);
        const bool isMSBShadow = !useSpriteWindow && BitTest(spriteOutput.a, kPixelAttrBitSpriteShadowWindow);
        if (isNormalShadow || isMSBShadow) {
            output >>= 1;
        }
    }

    if (IsColorOffsetEnabled(layerStack[0])) {
        const int3 offset = GetColorOffset(layerStack[0]);
        output = clamp(int3(output) + offset, 0, 255);
    }

    return output;
}

[numthreads(32, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint2 drawCoord = uint2(id.x, id.y + ScaleUp(config.startY));
    const uint2 outCoord = uint2(drawCoord.x, GetOutputY(drawCoord.y));
    const uint3 outColor = Compose(drawCoord);
    textureOut[outCoord] = float4(outColor / 255.0, 1.0f);
}
