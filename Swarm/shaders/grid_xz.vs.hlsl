#include "common.hlsli"

struct GridVsToPs
{
    float4 clipPos : SV_POSITION;
    float3 posWS   : POSITION;
};

void main(in StandardVsInput vin, out GridVsToPs vout)
{
    float4 worldPos = mul(g_world, float4(vin.pos, 1.0));
    vout.posWS = worldPos.xyz;
    vout.clipPos = mul(g_proj, mul(g_view, worldPos));
}
