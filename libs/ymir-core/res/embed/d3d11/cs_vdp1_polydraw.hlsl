// TODO: inputs and outputs:
// [cbuf] rendering parameters - number of polygons to render, relevant VDP1 registers
// [in] VDP1 VRAM
// [in] structured buffer array for the parameters of each polygon to render
// [out] 2D texture array for the rendered polygons - alpha channel contains sprite attributes

[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    // TODO: draw sprites
    // - id.z selects a sprite to draw
    // - id.xy are not used
    // - sprites are drawn sequentially due to the forward rendering method used by the Saturn
    // - each thread draws one full sprite into a dedicated framebuffer texture slice
    // TODO: figure out if it is possible to efficiently parallelize individual sprite rendering
    // TODO: 2D bin pack multiple polygons per framebuffer slice
}
