// Depth of Field - Vertical blur + composite pass
// Applies the second separable Gaussian pass (along Y) to the horizontally
// blurred texture, then composites the result with the original sharp colour
// based on each pixel's |CoC|.

Texture2D<float4> HBlurTex : register(t0);
Texture2D<float4> SharpTex : register(t1);
Texture2D<float>  CoCTex   : register(t2);

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
        OutTex[coord] = SharpTex[coord];
        return;
    }

    float  sigma      = max(absCoc * 0.5, 0.5);
    float4 colorSum   = 0.0;
    float  weightSum  = 0.0;
    bool   isFarField = centerCoc > 0.0;

    for (int y = -radius; y <= radius; y++)
    {
        int2   sampleCoord  = clamp(coord + int2(0, y), int2(0, 0), dims - 1);
        float  sampleCoc    = CoCTex[sampleCoord];
        float  sampleAbsCoc = abs(sampleCoc);

        float w = GaussWeight((float)y, sigma);

        if (isFarField && sampleAbsCoc < abs((float)y))
            w *= sampleAbsCoc / max(abs((float)y), 1.0);

        colorSum  += HBlurTex[sampleCoord] * w;
        weightSum += w;
    }

    float4 blurred = colorSum / max(weightSum, 1e-6);

    // Blend factor: 0 = in focus, 1 = fully blurred
    float blend = saturate(absCoc / max(MaxBlurRadius, 1.0));

    // Near-field gets slight priority (looks more correct physically)
    if (!isFarField)
        blend = saturate(blend * 1.2);

    float4 sharp = SharpTex[coord];
    // Preserve original alpha channel (e.g. TAA mask)
    float4 result  = lerp(sharp, blurred, blend);
    result.a       = sharp.a;

    OutTex[coord] = result;
}
