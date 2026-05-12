#ifndef SWARM_COMMON_HLSLI
#define SWARM_COMMON_HLSLI

cbuffer ViewConstants
{
    float4x4 g_view;
    float4x4 g_proj;
    float4   g_eyeWorld;
    float4   g_sunDirection;
    float4   g_sunColor;
    float4   g_ambient;
};

cbuffer ObjectConstants
{
    float4x4 g_world;
    float4x4 g_worldInvTranspose;
    uint     g_materialIndex;
    uint     _g_pad0;
    uint     _g_pad1;
    uint     _g_pad2;
};

cbuffer TimeConstants
{
    float4 g_timeConstants;
};
#define g_timeSeconds  g_timeConstants.x
#define g_deltaSeconds g_timeConstants.y

struct StandardVsInput
{
    float3 pos     : ATTRIB0;
    float3 normal  : ATTRIB1;
    float4 tangent : ATTRIB2;
    float2 uv      : ATTRIB3;
    float4 color   : ATTRIB4;
};

struct StandardVsToPs
{
    float4 clipPos  : SV_POSITION;
    float3 normalWS : NORMAL;
    float3 posWS    : POSITION;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD0;
};

#endif
