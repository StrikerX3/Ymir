// TODO: inputs and outputs:
// [cbuf] rendering parameters - relevant VDP1 registers (erase, vblank bounds)
// [in] VDP1 FBRAM
// [out] byte address buffer for framebuffer (+ transparent meshes buffer)

[numthreads(32, 32, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    // TODO: perform erase process
    // - id.xy are framebuffer coordinates
    // - id.z is not used
    // - either write the 16-bit erase value or copy from VDP1 FBRAM
    //  - output is in the same format as FBRAM
}
