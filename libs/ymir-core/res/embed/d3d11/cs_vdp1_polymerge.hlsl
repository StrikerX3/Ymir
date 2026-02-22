// TODO: inputs and outputs:
// [cbuffer] rendering parameters - number of polygons to merge, relevant VDP1 registers
// [StructuredBuffer array] parameters of each polygon to merge
// [ByteAddressBuffer] rendered polygons
// [RWByteAddressBuffer] byte address buffer for framebuffer (+ transparent meshes buffer)
// [RWByteAddressBuffer] VDP1 FBRAM

[numthreads(32, 32, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    // TODO: merge sprites
    // - id.xy are framebuffer coordinates
    // - id.z is not used
    // - merge sprites sequentially on top of the existing framebuffer textures
}
