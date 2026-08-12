struct PSInput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

struct DrawTextureConstants {
    float4 srcRect;
    float4 dstRect;
    float2 anchorPoint;
    float rotAngle;
};
