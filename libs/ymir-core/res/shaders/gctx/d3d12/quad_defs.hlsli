struct PSInput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

struct DrawTextureConstants {
    float4 srcRect;
    float4 dstRect;
    float2 renderTargetSize;
    float2 rotPivot;
    float rotAngle;
};

float2 Rotate2D(float2 p, float angle) {
    const float c = cos(angle);
    const float s = sin(angle);

    return float2(
        p.x * c - p.y * s,
        p.x * s + p.y * c
    );
}

float2 Rotate2DPivot(float2 p, float angle, float2 pivot = float2(0.5, 0.5)) {
    const float2 shifted = p - pivot;
    const float2 rotated = Rotate2D(shifted, angle);
    return rotated + pivot;
}
