#include "common.hlsli"

struct GizmoVsToPs
{
    float4 clipPos : SV_POSITION;
    float3 posWS   : POSITION;
    float4 color   : COLOR;
};

void main(in StandardVsInput vin, out GizmoVsToPs vout)
{
    float4 worldPos = mul(g_world, float4(vin.pos, 1.0));
    vout.clipPos = mul(g_proj, mul(g_view, worldPos));
    vout.posWS = worldPos.xyz;
    vout.color = vin.color;
}
