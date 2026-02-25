struct Config {
    uint numPolys;
    uint params;
    uint erase;
    uint _reserved;
};

// -----------------------------------------------------------------------------

cbuffer Config : register(b0) {
    Config config;
}

ByteAddressBuffer fbram : register(t0);

RWByteAddressBuffer fbOut : register(u0);

// -----------------------------------------------------------------------------

uint BitExtract(uint value, uint offset, uint length) {
    const uint mask = (1u << length) - 1u;
    return (value >> offset) & mask;
}

uint ByteSwap16(uint val) {
    return ((val >> 8) & 0x00FF) |
           ((val << 8) & 0xFF00);
}

uint ReadFBRAM16(uint address) {
    return ByteSwap16(BitExtract(fbram.Load(address & ~3), (address & 2) * 8, 16));
}

void WriteFBOut16(uint address, uint data) {
    const uint shift = (address & 2) * 8;
    const uint mask = 0xFFFF << shift;
    data = (data & 0xFFFF) << shift;
             
    address &= ~3;
    uint dummy;
    fbOut.InterlockedAnd(address, mask, dummy);
    fbOut.InterlockedOr(address, data, dummy);
}

// -----------------------------------------------------------------------------

static const uint fbSizeH = 512 << BitExtract(config.params, 0, 1);

[numthreads(32, 32, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    // TODO: perform erase/swap process
    // - id.xy are framebuffer coordinates
    // - id.z is not used
    // - either write the 16-bit erase value or copy from VDP1 FBRAM
    //  - output is in the same format as FBRAM
    
    // TODO: use erase parameters
    // TODO: framebuffer dimensions
    const uint address = (id.x + id.y * fbSizeH) * 2;
    //WriteFBOut16(address, ReadFBRAM16(address));
    WriteFBOut16(address, 0x0000);
}
