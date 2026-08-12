#include "quad_defs.hlsli"

cbuffer Constants : register(b0) {
    DrawTextureConstants g_consts;
};

PSInput VSMain(float4 position : POSITION, float2 uv : TEXCOORD) {
    PSInput result;

    // TODO: imitate SDL_RenderTextureRotated:
    // - srcRect specifies the source texture region to copy from (in texels)
    // - dstRect specifies the destination texture region to copy to (in texels)
    // - rotAngle is the clockwise rotation angle (in degrees)
    // - anchorPoint is the rotation anchor point. If null, use the center of the destination rectangle

    // TODO: use these to position the quad on the screen:
    // g_consts.dstRect;
    // g_consts.anchorPoint;
    // g_consts.rotAngle;
    result.position = position;
    // TODO: use this to adjust the texture coordinates:
    // g_consts.srcRect;
    result.uv = uv;

    return result;
}
