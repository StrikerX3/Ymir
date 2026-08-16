#ifndef YMIR_VDP2_NBG_HLSLI
#define YMIR_VDP2_NBG_HLSLI

#include "vdp_defs.hlsli"

uint4 DrawNBG(uint2 pos, uint index) {
    return uint4(pos.x, pos.y, index, 0);
}

#endif
