// TODO: inputs and outputs:
// [cbuffer] rendering parameters - number of polygons to render, relevant VDP1 registers
// [ByteAddressBuffer] VDP1 VRAM
// [StructuredBuffer array] parameters of each polygon to render
// [RWByteAddressBuffer] rendered polygons

[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    // TODO: draw sprites
    // - id.z selects a sprite to draw
    // - id.xy are not used
    // - sprites are drawn sequentially due to the forward rendering method used by the Saturn
    // - each thread draws one full sprite into a dedicated framebuffer texture slice
    //   - must clear target area before rendering sprite
    // TODO: figure out if it is possible to efficiently parallelize individual sprite rendering
    // TODO: 2D bin pack multiple polygons per framebuffer slice
}
