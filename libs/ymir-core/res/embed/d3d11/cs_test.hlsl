#define ANTIALIAS 0
#define ANTIALIAS_SAMPLES_X 2
#define ANTIALIAS_SAMPLES_Y 2

RWTexture2D<float4> textureOut;
cbuffer Consts : register(b0)
{
    float4 colors[256];
    double4 rect;
    uint vertLinePos;
};

static const float k = 0.003125; // 1/320

float4 calc(double2 pos)
{
    double dx, dy;
    double p, q;
    double x, y, xnew, ynew, d = 0; // use double to avoid precision limit for a bit longer while going deeper in the fractal
    uint iter = 0;
    dx = rect[2] - rect[0];
    dy = rect[3] - rect[1];
    p = rect[0] + pos.x * k * dx;
    q = rect[1] + pos.y * k * dy;
    x = p;
    y = q;
    while (iter < 255 && d < 4)
    {
        xnew = x * x - y * y + p;
        ynew = 2 * x * y + q;
        x = xnew;
        y = ynew;
        d = x * x + y * y;
        iter++;
    }
    return colors[iter];
}

#if ANTIALIAS

static const uint2 samples = uint2(ANTIALIAS_SAMPLES_X, ANTIALIAS_SAMPLES_Y);

[numthreads(16, 16, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x == vertLinePos)
    {
        textureOut[id.xy] = float4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    else
    {
        float4 colorOut = float4(0.0f, 0.0f, 0.0f, 0.0f);
        for (int y = 0; y < samples.x; y++)
        {
            for (int x = 0; x < samples.y; x++)
            {
                double2 coord = double2(x, y);
                colorOut = colorOut + calc(double2(id.xy) + coord / samples);
            }
        }
        textureOut[id.xy] = colorOut / samples.x / samples.y;
    }
}

#else

[numthreads(16, 16, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x == vertLinePos)
    {
        textureOut[id.xy] = float4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    else
    {
        textureOut[id.xy] = calc(double2(id.xy));
    }
}

#endif
