#ifndef YMIR_VDP_VDP2_RENDER_PARAMS_HLSLI
#define YMIR_VDP_VDP2_RENDER_PARAMS_HLSLI

struct RenderParams {
    // uint displayParams;
    // uint startY;
    // uint extraParams;
    // uint vcellScrollParams;
    // uint2 spriteParams;
    // uint windows;
    // uint scale;
};

cbuffer RenderParams : register(b0) {
    RenderParams g_renderParams;
}

#endif
