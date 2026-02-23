struct Config {
    uint4 _reserved;
};

struct PolyParams {
    uint atlasPos;
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

RWByteAddressBuffer polyOut : register(u0);

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
    return uint2(
        BitExtract(value, 0, 16),
        BitExtract(value, 16, 16)
    );
}

// -----------------------------------------------------------------------------

void DrawNormalSprite(uint index, const PolyParams poly, const uint cmdctrl) {
}

void DrawScaledSprite(uint index, const PolyParams poly, const uint cmdctrl) {
}

void DrawDistortedSprite(uint index, const PolyParams poly, const uint cmdctrl) {
}

void DrawPolygon(uint index, const PolyParams poly) {
}

void DrawPolylines(uint index, const PolyParams poly) {
}

void DrawLine(uint index, const PolyParams poly) {
}

void Draw(uint index) {
    const PolyParams poly = polyParams[index];
    // const uint atlasPosX = BitExtract(poly.atlasPos, 0, 16);
    // const uint atlasPosY = BitExtract(poly.atlasPos, 16, 16);
    // const uint sysClipH = BitExtract(poly.sysClip, 0, 16);
    // const uint sysClipV = BitExtract(poly.sysClip, 16, 16);
    // const uint userClipX0 = BitExtract(poly.userClipX, 0, 16);
    // const uint userClipX1 = BitExtract(poly.userClipX, 16, 16);
    // const uint userClipY0 = BitExtract(poly.userClipY, 0, 16);
    // const uint userClipY1 = BitExtract(poly.userClipY, 16, 16);
    // const uint localCoordX = BitExtract(poly.localCoord, 0, 16);
    // const uint localCoordY = BitExtract(poly.localCoord, 16, 16);

    // TODO: determine actual bounds and offset into atlas
    
    const uint cmdctrl = FetchCMDSingle(poly.cmdAddress, kOffsetCMDCTRL);
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

    polyOut.Store(index * 16, poly.atlasPos);
    polyOut.Store(index * 16 + 4, poly.cmdAddress);
    polyOut.Store(index * 16 + 8, cmdctrl);
    polyOut.Store(index * 16 + 12, 0xDEADBEEF);
}

// -----------------------------------------------------------------------------

[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    // TODO: figure out if it is possible to efficiently parallelize individual polygon rendering
    Draw(id.z);
}
