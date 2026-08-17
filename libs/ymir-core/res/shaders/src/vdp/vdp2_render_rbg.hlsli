#ifndef YMIR_VDP_VDP2_RBG_HLSLI
#define YMIR_VDP_VDP2_RBG_HLSLI

#include "vdp2_defs.hlsli"

uint4 DrawRBG(uint2 pos, uint index) {
    return uint4(pos.x, pos.y, index, 1);
}

#endif
