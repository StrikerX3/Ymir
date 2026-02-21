// TODO: inputs and outputs:
// [cbuf] rendering parameters - number of polygons to merge, relevant VDP1 registers
// [in] structured buffer array for the parameters of each polygon to merge
// [in] 2D texture array with rendered polygons
// [out] 2D texture array for framebuffer (+ transparent meshes buffer)
// [out] VDP1 FBRAM

[numthreads(32, 32, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    // TODO: merge sprites
    // - id.xy are framebuffer coordinates
    // - id.z is not used
    // - merge sprites sequentially on top of the existing framebuffer textures
}
