#include "common.hlsli"

struct GizmoVsToPs
{
    float4 clipPos : SV_POSITION;
    float3 posWS   : POSITION;
    float4 color   : COLOR;
};

struct PsOutput
{
    float4 color : SV_TARGET;
};

void main(in GizmoVsToPs pin, out PsOutput pout)
{
    // alpha < 0.75 marks a ring (no axis fade); >= 0.75 marks an axis handle.
    bool isAxisHandle = pin.color.a > 0.75;

    float alpha = 1.0;
    if (isAxisHandle)
    {
        float3 axisDirWS = normalize(mul((float3x3)g_world, float3(1.0, 0.0, 0.0)));
        float3 viewDir = normalize(g_eyeWorld.xyz - pin.posWS);
        float align = abs(dot(viewDir, axisDirWS));
        alpha = 1.0 - smoothstep(0.96, 0.998, align);
    }

    pout.color = float4(pin.color.rgb, alpha);
}
