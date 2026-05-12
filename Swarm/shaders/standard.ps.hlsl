#include "common.hlsli"

struct MaterialParams
{
    float4 base_color_factor;
    float  metallic_factor;
    float  roughness_factor;
    float2 _pad0;
    float4 emissive_factor;
    uint   albedo_map_index;
    uint   normal_map_index;
    uint   metallic_roughness_map_index;
    uint   _pad_indices;
};

StructuredBuffer<MaterialParams> g_materials       : register(t0, space2);
Texture2D                        g_textures[8192]  : register(t0, space1);
SamplerState                     g_textures_sampler : register(s0, space1);

struct PsOutput
{
    float4 color : SV_TARGET;
};

static const float kPi = 3.14159265358979;

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float DistributionGGX(float ndoth, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = ndoth * ndoth * (a2 - 1.0) + 1.0;
    return a2 / max(kPi * denom * denom, 1e-5);
}

float GeometrySchlickGGX(float ndotv, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) * 0.125;
    return ndotv / max(ndotv * (1.0 - k) + k, 1e-5);
}

float GeometrySmith(float ndotv, float ndotl, float roughness)
{
    return GeometrySchlickGGX(ndotv, roughness) * GeometrySchlickGGX(ndotl, roughness);
}

float4 SampleBindless(uint slot, float2 uv)
{
    return g_textures[slot].Sample(g_textures_sampler, uv);
}

float3 ApplyNormalMap(uint slot, float3 N, float3 posWS, float2 uv)
{
    float3 sampledN = SampleBindless(slot, uv).xyz * 2.0 - 1.0;

    float3 dp1 = ddx(posWS);
    float3 dp2 = ddy(posWS);
    float2 du1 = ddx(uv);
    float2 du2 = ddy(uv);
    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    float3 T = dp2perp * du1.x + dp1perp * du2.x;
    float3 B = dp2perp * du1.y + dp1perp * du2.y;
    float invMax = rsqrt(max(dot(T, T), dot(B, B)));
    return normalize(sampledN.x * T * invMax + sampledN.y * B * invMax + sampledN.z * N);
}

void main(in StandardVsToPs pin, out PsOutput pout)
{
    MaterialParams mat = g_materials[g_materialIndex];

    float4 albedoTex = SampleBindless(mat.albedo_map_index, pin.uv);
    float3 albedo = mat.base_color_factor.rgb * albedoTex.rgb * pin.color.rgb;
    float alpha = mat.base_color_factor.a * albedoTex.a;

    float4 mrTex = SampleBindless(mat.metallic_roughness_map_index, pin.uv);
    float metallic = saturate(mat.metallic_factor * mrTex.b);
    float roughness = max(saturate(mat.roughness_factor * mrTex.g), 0.04);

    float3 N = ApplyNormalMap(mat.normal_map_index, normalize(pin.normalWS), pin.posWS, pin.uv);
    float3 V = normalize(g_eyeWorld.xyz - pin.posWS);
    float3 L = normalize(-g_sunDirection.xyz);
    float3 H = normalize(V + L);

    float ndotl = saturate(dot(N, L));
    float ndotv = saturate(dot(N, V));
    float ndoth = saturate(dot(N, H));
    float vdoth = saturate(dot(V, H));

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 F = FresnelSchlick(vdoth, F0);
    float D = DistributionGGX(ndoth, roughness);
    float G = GeometrySmith(ndotv, ndotl, roughness);

    float3 spec = (D * G) * F / max(4.0 * ndotv * ndotl, 1e-5);
    float3 kd = (1.0 - F) * (1.0 - metallic);
    float3 diffuse = kd * albedo / kPi;

    float3 direct = (diffuse + spec) * g_sunColor.rgb * ndotl;
    float3 ambient = albedo * g_ambient.rgb;
    float3 emissive = mat.emissive_factor.rgb;

    pout.color = float4(direct + ambient + emissive, alpha);
}
