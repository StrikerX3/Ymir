// TODO: inputs and outputs:
// [cbuffer] rendering parameters - relevant VDP1 registers (erase, vblank bounds)
// [ByteAddressBuffer] VDP1 FBRAM
// [RWByteAddressBuffer] framebuffer + transparent meshes

[numthreads(32, 32, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    // TODO: perform erase/swap process
    // - id.xy are framebuffer coordinates
    // - id.z is not used
    // - either write the 16-bit erase value or copy from VDP1 FBRAM
    //  - output is in the same format as FBRAM
}
