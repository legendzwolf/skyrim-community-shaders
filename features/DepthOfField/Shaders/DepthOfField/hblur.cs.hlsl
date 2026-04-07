// Depth of Field - Horizontal blur pass
// Samples the scene colour along X weighted by a variable-radius Gaussian
// derived from each pixel's circle-of-confusion (CoC).
//
// Weighting strategy:
//  • Far-field (CoC > 0): downweight sharp neighbours so they don't bleed
//    into a blurry background.
//  • Near-field (CoC < 0): allow all neighbours at full weight so bokeh
//    discs bleed outward naturally.

Texture2D<float4> ColorTex : register(t0);
Texture2D<float>  CoCTex   : register(t1);

RWTexture2D<float4> OutTex : register(u0);

cbuffer DoFCB : register(b1)
{
    float FocusDistance;
    float FocusRange;
    float MaxBlurRadius;
    float NearBlurScale;
    float FarBlurScale;
    float3 _pad;
};

#include "Common/SharedData.hlsli"

static const int MAX_KERNEL_RADIUS = 16;

float GaussWeight(float x, float sigma)
{
    return exp(-0.5 * x * x / (sigma * sigma + 1e-4));
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    int2 coord = (int2)tid.xy;
    int2 dims  = (int2)SharedData::BufferDim.xy;
    if (any(coord >= dims))
        return;

    float centerCoc = CoCTex[coord];
    float absCoc    = abs(centerCoc);
    int   radius    = min((int)absCoc, MAX_KERNEL_RADIUS);

    if (radius < 1)
    {
        OutTex[coord] = ColorTex[coord];
        return;
    }

    float  sigma      = max(absCoc * 0.5, 0.5);
    float4 colorSum   = 0.0;
    float  weightSum  = 0.0;
    bool   isFarField = centerCoc > 0.0;

    for (int x = -radius; x <= radius; x++)
    {
        int2   sampleCoord  = clamp(coord + int2(x, 0), int2(0, 0), dims - 1);
        float  sampleCoc    = CoCTex[sampleCoord];
        float  sampleAbsCoc = abs(sampleCoc);

        float w = GaussWeight((float)x, sigma);

        // For far-field blur: reduce the influence of sharper (smaller-CoC)
        // neighbours proportionally so they don't contaminate the blur.
        if (isFarField && sampleAbsCoc < abs((float)x))
            w *= sampleAbsCoc / max(abs((float)x), 1.0);

        colorSum  += ColorTex[sampleCoord] * w;
        weightSum += w;
    }

    OutTex[coord] = colorSum / max(weightSum, 1e-6);
}
