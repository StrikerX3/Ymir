#include "quad_defs.hlsli"

cbuffer Constants : register(b0) {
    DrawTextureConstants g_consts;
};

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

float4 PSMain(PSInput input) : SV_TARGET {
    // TODO: use this to adjust the texture's UV:
    // g_consts.srcRect;
    return g_texture.Sample(g_sampler, input.uv);
}
