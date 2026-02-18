struct Config {
    uint displayParams;
    uint startY;
    uint layerEnabled;
    uint _padding;
};

cbuffer Config : register(b0) {
    Config config;
}

// -----------------------------------------------------------------------------

Texture2DArray<uint4> bgIn : register(t0); // [0-3] = NBG0-3, [4-5] = RBG0-1
// TODO: spriteIn (normal and mesh layers)
// TODO: composeParams

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
//  0-3  Priority (0 to 7)
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

uint4 GetLayerOutput(uint index, uint2 pos) {
    switch (index) {
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
    
    // TODO: sprite mesh layer
    
    const uint4 topOut = GetLayerOutput(layerStack[0], pos);
    return topOut.rgb;
}

[numthreads(32, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    // TODO: compose image
   
    const uint2 drawCoord = uint2(id.x, GetY(id.y) + config.startY);
    const uint3 outColor = Compose(drawCoord);
    textureOut[drawCoord] = float4(outColor / 255.0, 1.0f);
}
