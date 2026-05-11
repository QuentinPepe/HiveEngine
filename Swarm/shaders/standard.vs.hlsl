#include "common.hlsli"

void main(in StandardVsInput vin, out StandardVsToPs vout)
{
    float4 worldPos = mul(g_world, float4(vin.pos, 1.0));
    vout.posWS = worldPos.xyz;
    vout.clipPos = mul(g_proj, mul(g_view, worldPos));
    vout.normalWS = normalize(mul((float3x3) g_worldInvTranspose, vin.normal));
    vout.color = vin.color;
    vout.uv = vin.uv;
}
