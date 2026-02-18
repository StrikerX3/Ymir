struct Config {
    uint displayParams;
    uint startY;
    uint layerEnabled;
    uint _reserved;
};

struct ComposeParams {
    uint params;
    int3 colorOffsetA;
    int3 colorOffsetB;
    uint _reserved;
};

// -----------------------------------------------------------------------------

cbuffer Config : register(b0) {
    Config config;
}

Texture2DArray<uint4> bgIn : register(t0); // [0-3] = NBG0-3, [4-5] = RBG0-1
// TODO: spriteIn (normal and mesh layers)
StructuredBuffer<ComposeParams> composeParams : register(t1);

RWTexture2D<float4> textureOut : register(u0);

// -----------------------------------------------------------------------------

static const uint kBGLayerNBG0 = 0;
static const uint kBGLayerNBG1 = 1;
static const uint kBGLayerNBG2 = 2;
static const uint kBGLayerNBG3 = 3;
static const uint kBGLayerRBG0 = 4;
static const uint kBGLayerRBG1 = 5;

static const uint kLayerSprite = 0;
static const uint kLayerRBG0 = 1;
static const uint kLayerNBG0_RBG1 = 2;
static const uint kLayerNBG1_EXBG = 3;
static const uint kLayerNBG2 = 4;
static const uint kLayerNBG3 = 5;
static const uint kLayerBack = 6;
static const uint kLayerLine = 7; // not used in the stack, but referenced for parameters

static const uint4 kTransparentPixel = uint4(0, 0, 0, 128);

// -----------------------------------------------------------------------------

bool BitTest(uint value, uint bit) {
    return ((value >> bit) & 1) != 0;
}

uint BitExtract(uint value, uint offset, uint length) {
    const uint mask = (1u << length) - 1u;
    return (value >> offset) & mask;
}

// The alpha channel of the layer textures contains pixel attributes:
// bits  use
//  0-2  Priority (0 to 7)
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
    attrs.specColorCalc = BitTest(pixelData, 6);
    attrs.transparent = BitTest(pixelData, 7);
    return attrs;
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

// -----------------------------------------------------------------------------

bool IsBGLayerEnabled(uint bgLayer) {
    return BitTest(config.layerEnabled, bgLayer + 16);
}

bool IsLayerEnabled(uint layer) {
    return BitTest(config.layerEnabled, layer);
}

bool IsColorCalcEnabled(uint layer, uint2 pos) {
    if (layer > kLayerBack) {
        return false;
    }
    if (!BitTest(composeParams[0].params.x, layer)) {
        return false;
    }
    if (layer == kLayerSprite) {
        // TODO: check sprite pixel attributes at [pos]
    }
    return true;
}

bool IsLineColorEnabled(uint layer, uint2 pos) {
    // TODO: implement
    return false;
}

int GetColorCalcRatio(uint layer, uint2 pos) {
    // TODO: implement
    return 15;
}

bool IsColorOffsetEnabled(uint layer) {
    return BitTest(composeParams[0].params.x, layer + 11);
}

int3 GetColorOffset(uint layer) {
    const bool selB = BitTest(composeParams[0].params, 18 + layer);
    return selB ? composeParams[0].colorOffsetB : composeParams[0].colorOffsetA;
}

uint4 GetLayerOutput(uint layer, uint2 pos) {
    switch (layer) {
        case kLayerSprite:
            return kTransparentPixel; // TODO: read from sprite layer output
        case kLayerRBG0:
            return bgIn[uint3(pos.xy, kBGLayerRBG0)];
        case kLayerNBG0_RBG1:
            return bgIn[uint3(pos.xy, IsBGLayerEnabled(kBGLayerRBG1) ? kBGLayerRBG1 : kBGLayerNBG0)];
        case kLayerNBG1_EXBG:
            return bgIn[uint3(pos.xy, kBGLayerNBG1)];
        case kLayerNBG2:
            return bgIn[uint3(pos.xy, kBGLayerNBG2)];
        case kLayerNBG3:
            return bgIn[uint3(pos.xy, kBGLayerNBG3)];
        case kLayerBack:
            return kTransparentPixel; // TODO: read from back screen output
        default:
            return kTransparentPixel; // should never happpen
    }
}

uint3 Compose(uint2 pos) {
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
        
        // TODO: skip normal shadow sprite layer pixels

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
    
    // TODO: find sprite mesh layer stack position
    
    uint3 output = { 0, 0, 0 };
    
    const bool layer0ColorCalcEnabled = IsColorCalcEnabled(layerStack[0], pos);
    const bool layer0LineColorEnabled = IsLineColorEnabled(layerStack[0], pos);
    const bool extendedColorCalc = BitTest(composeParams[0].params.x, 8);
    const bool useAdditiveBlend = BitTest(composeParams[0].params.x, 9);
    const bool useSecondScreenRatio = BitTest(composeParams[0].params.x, 10);

    const uint4 layer0Pixel = GetLayerOutput(layerStack[0], pos);
    uint4 layer1Pixel = GetLayerOutput(layerStack[1], pos);
    
    // TODO: line color data
    
    if (extendedColorCalc) {
        if (IsColorCalcEnabled(layerStack[1], pos)) {
            const uint4 layer2Pixel = GetLayerOutput(layerStack[2], pos);

            // TODO: blend layer 2 with sprite mesh layer colors
            
            layer1Pixel.rgb = (layer1Pixel.rgb + layer2Pixel.rgb) >> 1;
        }
        
        if (layer0LineColorEnabled) {
            if (IsColorCalcEnabled(kLayerLine, pos)) {
                // TODO: blend line color
            } else {
                // TODO: replace with line color
            }
        }
    } else {
        if (layer0LineColorEnabled) {
            // TODO: replace layer 1 pixel with line color screen
        }
    }
    
    // TODO: blend layer 1 with sprite mesh layer colors
    
    if (useAdditiveBlend) {
        output = min(layer0Pixel.rgb + layer1Pixel.rgb, 255);
    } else {
        if (layer0ColorCalcEnabled) {
            const uint ratioLayer = useSecondScreenRatio ? layerStack[1] : layerStack[0];
            const int ratio = GetColorCalcRatio(ratioLayer, pos);
            output = int3(layer1Pixel.rgb) + (int3(layer0Pixel.rgb) - int3(layer1Pixel.rgb)) * ratio / 32;
        } else {
            output = layer0Pixel.rgb;
        }
    }
    
    // TODO: blend layer 0 with sprite mesh layer colors
    
    // TOOD: apply sprite shadow
    
    if (IsColorOffsetEnabled(layerStack[0])) {
        const int3 offset = GetColorOffset(layerStack[0]);
        output = clamp(int3(output) + offset, 0, 255);
    }
    
    return output;
}

[numthreads(32, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint2 drawCoord = uint2(id.x, GetY(id.y) + config.startY);
    const uint3 outColor = Compose(drawCoord);
    textureOut[drawCoord] = float4(outColor / 255.0, 1.0f);
}
