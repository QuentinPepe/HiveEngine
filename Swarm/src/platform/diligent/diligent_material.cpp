#include <swarm/platform/diligent_swarm.h>
#include <swarm/precomp.h>
#include <swarm/swarm.h>
#include <swarm/swarm_log.h>
#include <swarm/swarmmodule.h>

#include <hive/core/log.h>

#include <diligent_internal.h>

#include <GraphicsTypes.h>
#include <InputLayout.h>
#include <PipelineState.h>
#include <RefCntAutoPtr.hpp>
#include <Shader.h>

namespace swarm
{
    namespace
    {
        const char* g_standardVertexShader = R"(
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
};

struct VsInput
{
    float3 pos     : ATTRIB0;
    float3 normal  : ATTRIB1;
    float4 tangent : ATTRIB2;
    float2 uv      : ATTRIB3;
    float4 color   : ATTRIB4;
};

struct VsOutput
{
    float4 clipPos  : SV_POSITION;
    float3 normalWS : NORMAL;
    float3 posWS    : POSITION;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD0;
};

void main(in VsInput vin, out VsOutput vout)
{
    float4 worldPos = mul(g_world, float4(vin.pos, 1.0));
    vout.posWS = worldPos.xyz;
    vout.clipPos = mul(g_proj, mul(g_view, worldPos));
    vout.normalWS = normalize(mul((float3x3) g_worldInvTranspose, vin.normal));
    vout.color = vin.color;
    vout.uv = vin.uv;
}
)";

        const char* g_standardPixelShader = R"(
cbuffer ViewConstants
{
    float4x4 g_view;
    float4x4 g_proj;
    float4   g_eyeWorld;
    float4   g_sunDirection;
    float4   g_sunColor;
    float4   g_ambient;
};

struct PsInput
{
    float4 clipPos  : SV_POSITION;
    float3 normalWS : NORMAL;
    float3 posWS    : POSITION;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD0;
};

struct PsOutput
{
    float4 color : SV_TARGET;
};

void main(in PsInput pin, out PsOutput pout)
{
    float3 albedo = pin.color.rgb;
    float3 n = normalize(pin.normalWS);
    // g_sunDirection is the direction the light travels — flip for surface->light vector.
    float3 toLight = normalize(-g_sunDirection.xyz);
    float ndotl = saturate(dot(n, toLight));
    float3 lit = albedo * (g_ambient.rgb + g_sunColor.rgb * ndotl);
    pout.color = float4(lit, 1.0);
}
)";
    } // namespace

    Material* CreateStandardMaterial(RenderContext* context)
    {
        using namespace Diligent;

        if (context == nullptr || context->m_device == nullptr || context->m_swapchain == nullptr)
        {
            return nullptr;
        }
        if (context->m_viewConstantBuffer == nullptr || context->m_objectConstantBuffer == nullptr)
        {
            hive::LogError(LOG_SWARM, "CreateStandardMaterial: scene constants are not initialized");
            return nullptr;
        }

        ShaderCreateInfo shaderCreateInfo;
        shaderCreateInfo.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        shaderCreateInfo.Desc.UseCombinedTextureSamplers = true;

        RefCntAutoPtr<IShader> vertexShader;
        {
            shaderCreateInfo.Desc.ShaderType = SHADER_TYPE_VERTEX;
            shaderCreateInfo.EntryPoint = "main";
            shaderCreateInfo.Desc.Name = "Swarm Standard VS";
            shaderCreateInfo.Source = g_standardVertexShader;
            context->m_device->CreateShader(shaderCreateInfo, &vertexShader);
        }
        if (!vertexShader)
        {
            hive::LogError(LOG_SWARM, "CreateStandardMaterial: failed to compile vertex shader");
            return nullptr;
        }

        RefCntAutoPtr<IShader> pixelShader;
        {
            shaderCreateInfo.Desc.ShaderType = SHADER_TYPE_PIXEL;
            shaderCreateInfo.EntryPoint = "main";
            shaderCreateInfo.Desc.Name = "Swarm Standard PS";
            shaderCreateInfo.Source = g_standardPixelShader;
            context->m_device->CreateShader(shaderCreateInfo, &pixelShader);
        }
        if (!pixelShader)
        {
            hive::LogError(LOG_SWARM, "CreateStandardMaterial: failed to compile pixel shader");
            return nullptr;
        }

        // Input layout must match swarm::Vertex.
        LayoutElement layoutElements[] = {
            LayoutElement{0, 0, 3, VT_FLOAT32, False},
            LayoutElement{1, 0, 3, VT_FLOAT32, False},
            LayoutElement{2, 0, 4, VT_FLOAT32, False},
            LayoutElement{3, 0, 2, VT_FLOAT32, False},
            LayoutElement{4, 0, 4, VT_UINT8, True},
        };

        GraphicsPipelineStateCreateInfo pipelineCreateInfo;
        pipelineCreateInfo.PSODesc.Name = "Swarm Standard PSO";
        pipelineCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
        pipelineCreateInfo.GraphicsPipeline.NumRenderTargets = 1;
        pipelineCreateInfo.GraphicsPipeline.RTVFormats[0] = context->m_swapchain->GetDesc().ColorBufferFormat;
        pipelineCreateInfo.GraphicsPipeline.DSVFormat = context->m_swapchain->GetDesc().DepthBufferFormat;
        pipelineCreateInfo.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        pipelineCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_BACK;
        pipelineCreateInfo.GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = True;
        pipelineCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = True;
        pipelineCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = layoutElements;
        pipelineCreateInfo.GraphicsPipeline.InputLayout.NumElements = _countof(layoutElements);
        pipelineCreateInfo.pVS = vertexShader;
        pipelineCreateInfo.pPS = pixelShader;

        // Constant buffers are bound globally for every draw, so mark them as STATIC.
        pipelineCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

        IPipelineState* pipelineState = nullptr;
        context->m_device->CreateGraphicsPipelineState(pipelineCreateInfo, &pipelineState);
        if (pipelineState == nullptr)
        {
            hive::LogError(LOG_SWARM, "CreateStandardMaterial: failed to create pipeline state");
            return nullptr;
        }

        if (auto* variable = pipelineState->GetStaticVariableByName(SHADER_TYPE_VERTEX, "ViewConstants"))
        {
            variable->Set(context->m_viewConstantBuffer);
        }
        if (auto* variable = pipelineState->GetStaticVariableByName(SHADER_TYPE_VERTEX, "ObjectConstants"))
        {
            variable->Set(context->m_objectConstantBuffer);
        }
        if (auto* variable = pipelineState->GetStaticVariableByName(SHADER_TYPE_PIXEL, "ViewConstants"))
        {
            variable->Set(context->m_viewConstantBuffer);
        }

        IShaderResourceBinding* resourceBinding = nullptr;
        pipelineState->CreateShaderResourceBinding(&resourceBinding, true);
        if (resourceBinding == nullptr)
        {
            pipelineState->Release();
            hive::LogError(LOG_SWARM, "CreateStandardMaterial: failed to create shader resource binding");
            return nullptr;
        }

        auto& allocator = SwarmModule::GetInstance().GetAllocator();
        auto* material = comb::New<Material>(allocator);
        material->m_pipelineState = pipelineState;
        material->m_resourceBinding = resourceBinding;
        return material;
    }

    void DestroyMaterial(Material* material)
    {
        if (material == nullptr)
        {
            return;
        }

        if (material->m_resourceBinding != nullptr)
        {
            material->m_resourceBinding->Release();
        }
        if (material->m_pipelineState != nullptr)
        {
            material->m_pipelineState->Release();
        }

        auto& allocator = SwarmModule::GetInstance().GetAllocator();
        comb::Delete(allocator, material);
    }
} // namespace swarm
