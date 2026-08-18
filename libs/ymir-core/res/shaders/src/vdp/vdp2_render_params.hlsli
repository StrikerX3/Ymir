#ifndef YMIR_VDP_VDP2_RENDER_PARAMS_HLSLI
#define YMIR_VDP_VDP2_RENDER_PARAMS_HLSLI

struct RenderParams {
    // uint startY;
};

cbuffer RenderParams : register(b0) {
    RenderParams g_renderParams;
}

#endif
