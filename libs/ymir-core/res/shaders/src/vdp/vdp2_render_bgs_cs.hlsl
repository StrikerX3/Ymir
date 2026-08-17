#include "vdp2_render_params.hlsli"

#include "vdp2_render_nbg.hlsli"
#include "vdp2_render_rbg.hlsli"

RWTexture2DArray<uint4> bgOut : register(u0);

[numthreads(32, 1, 6)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint2 drawCoord = uint2(id.x, id.y /*+ ScaleUp(config.startY)*/);
    const uint3 outCoord = uint3(drawCoord.x, drawCoord.y/*GetY(drawCoord.y, false)*/, id.z);
    if (id.z <= 3) {
        bgOut[outCoord] = DrawNBG(drawCoord, id.z);
    } else if (id.z <= 5) {
        bgOut[outCoord] = DrawRBG(drawCoord, id.z - 4);
    }
}
