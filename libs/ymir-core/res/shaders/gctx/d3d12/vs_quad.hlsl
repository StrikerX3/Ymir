#include "quad_defs.hlsli"

cbuffer Constants : register(b0) {
    DrawTextureConstants g_consts;
};

PSInput VSMain(float4 position : POSITION, float2 uv : TEXCOORD) {
    PSInput result;

    // TODO: use these to position the quad on the screen:
    // g_consts.dstRect;
    // g_consts.anchorPoint;
    // g_consts.rotAngle;
    result.position = position;
    result.uv = uv;

    return result;
}
