#ifndef YMIR_VDP_VDP2_NBG_HLSLI
#define YMIR_VDP_VDP2_NBG_HLSLI

#include "vdp2_defs.hlsli"
#include "vdp2_utils.hlsli"

#include "util/bit_ops.hlsli"

uint4 DrawNBG(uint2 pos, uint index) {
    return uint4(pos.x, pos.y, index, 0);
}

#endif
