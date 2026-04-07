// Depth of Field - Circle of Confusion pass
// Reads scene depth and outputs a signed CoC value per pixel (in pixels).
//   CoC > 0  →  pixel is behind the focal plane (far-field blur)
//   CoC < 0  →  pixel is in front of the focal plane (near-field blur)
//   CoC = 0  →  pixel is in focus

Texture2D<float> DepthTex : register(t0);
RWTexture2D<float> CoCTex : register(u0);

cbuffer DoFCB : register(b1)
{
    float FocusDistance;   // view-space focus distance (game units)
    float FocusRange;      // half-width of the in-focus band (game units)
    float MaxBlurRadius;   // maximum CoC radius in pixels
    float NearBlurScale;   // extra scale for near-field blur
    float FarBlurScale;    // extra scale for far-field blur
    float3 _pad;
};

#include "Common/SharedData.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (any(tid.xy >= (uint2)SharedData::BufferDim.xy))
        return;

    float rawDepth    = DepthTex[tid.xy].x;
    float linearDepth = SharedData::GetScreenDepth(rawDepth);

    // Normalised signed CoC in [-1, 1]
    float coc = (linearDepth - FocusDistance) / max(abs(FocusRange), 1.0);
    coc = clamp(coc, -1.0, 1.0);

    // Scale to pixels
    coc = coc > 0.0 ? coc * FarBlurScale  * MaxBlurRadius
                    : coc * NearBlurScale * MaxBlurRadius;

    CoCTex[tid.xy] = coc;
}
