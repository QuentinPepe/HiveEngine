#include "common.hlsli"

struct GridVsToPs
{
    float4 clipPos : SV_POSITION;
    float3 posWS   : POSITION;
};

struct PsOutput
{
    float4 color : SV_TARGET;
};

static const float3 kGridColor    = float3(0.20, 0.20, 0.20);
static const float3 kGridEmphasis = float3(0.50, 0.50, 0.50);
static const float3 kAxisColorX   = float3(0.85, 0.25, 0.25);
static const float3 kAxisColorZ   = float3(0.30, 0.50, 0.95);

float LineCoverage(float2 worldXZ, float step)
{
    float2 c = worldXZ / step;
    float2 grid = abs(frac(c - 0.5) - 0.5) / fwidth(c);
    return 1.0 - saturate(min(grid.x, grid.y));
}

void main(in GridVsToPs pin, out PsOutput pout)
{
    float2 worldXZ = pin.posWS.xz;

    // refDist must be frame-uniform — per-pixel evaluation makes the level discretization
    // visible as a ring whenever it crosses a decade boundary.
    float refDist = max(abs(g_eyeWorld.y), 1.0);

    float level = log10(refDist / 8.0);
    float i = floor(level);
    float f = saturate(level - i);

    float step0 = pow(10.0, i - 1.0);
    float step1 = pow(10.0, i);
    float step2 = pow(10.0, i + 1.0);

    float cov0 = LineCoverage(worldXZ, step0);
    float cov1 = LineCoverage(worldXZ, step1);
    float cov2 = LineCoverage(worldXZ, step2);

    float alpha0 = 1.0 - f;
    float3 color0 = kGridColor;
    float3 color1 = lerp(kGridColor, kGridEmphasis, 1.0 - f);
    float3 color2 = kGridEmphasis;

    float3 color = color0;
    float coverage = cov0 * alpha0;
    color = lerp(color, color1, cov1);
    coverage = max(coverage, cov1);
    color = lerp(color, color2, cov2);
    coverage = max(coverage, cov2);

    float axisX = 1.0 - saturate(abs(pin.posWS.z) / fwidth(pin.posWS.z));
    float axisZ = 1.0 - saturate(abs(pin.posWS.x) / fwidth(pin.posWS.x));
    color = lerp(color, kAxisColorX, axisX);
    color = lerp(color, kAxisColorZ, axisZ);
    coverage = max(coverage, max(axisX, axisZ));

    float3 viewVec = pin.posWS - g_eyeWorld.xyz;
    float distToFrag = length(viewVec);
    float verticality = abs(viewVec.y / max(distToFrag, 1e-6));

    float grazing = 1.0 - pow(1.0 - verticality, 3.0);

    float fadeEnd = refDist * 400.0;
    float distFade = 1.0 - saturate((distToFrag - 0.3 * fadeEnd) / (0.7 * fadeEnd));

    float alpha = coverage * grazing * distFade;
    pout.color = float4(color, alpha);
}
