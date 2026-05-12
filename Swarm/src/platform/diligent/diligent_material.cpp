#include <hive/core/log.h>

#include <wax/containers/string.h>
#include <wax/containers/vector.h>

#include <swarm/platform/diligent_swarm.h>
#include <swarm/precomp.h>
#include <swarm/swarm.h>
#include <swarm/swarm_log.h>
#include <swarm/swarmmodule.h>

#include <GraphicsTypes.h>
#include <InputLayout.h>
#include <PipelineState.h>
#include <RefCntAutoPtr.hpp>
#include <Shader.h>
#include <ShaderMacroHelper.hpp>
#include <ShaderSourceFactoryUtils.h>
#include <cstring>
#include <diligent_bindless.h>
#include <diligent_internal.h>
#include <diligent_materials_buffer.h>
#include <diligent_shader_library.h>

namespace swarm
{
    namespace
    {
        using namespace Diligent;

        CULL_MODE TranslateCullMode(CullMode mode)
        {
            switch (mode)
            {
                case CullMode::NONE:
                    return CULL_MODE_NONE;
                case CullMode::FRONT:
                    return CULL_MODE_FRONT;
                case CullMode::BACK:
                default:
                    return CULL_MODE_BACK;
            }
        }

        FILL_MODE TranslateFillMode(FillMode mode)
        {
            return (mode == FillMode::WIREFRAME) ? FILL_MODE_WIREFRAME : FILL_MODE_SOLID;
        }

        void ConfigureBlend(RenderTargetBlendDesc& target, BlendMode mode)
        {
            switch (mode)
            {
                case BlendMode::ALPHA_BLEND:
                    target.BlendEnable = True;
                    target.SrcBlend = BLEND_FACTOR_SRC_ALPHA;
                    target.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
                    target.BlendOp = BLEND_OPERATION_ADD;
                    target.SrcBlendAlpha = BLEND_FACTOR_ONE;
                    target.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
                    target.BlendOpAlpha = BLEND_OPERATION_ADD;
                    return;
                case BlendMode::ADDITIVE:
                    target.BlendEnable = True;
                    target.SrcBlend = BLEND_FACTOR_ONE;
                    target.DestBlend = BLEND_FACTOR_ONE;
                    target.BlendOp = BLEND_OPERATION_ADD;
                    target.SrcBlendAlpha = BLEND_FACTOR_ONE;
                    target.DestBlendAlpha = BLEND_FACTOR_ONE;
                    target.BlendOpAlpha = BLEND_OPERATION_ADD;
                    return;
                case BlendMode::OPAQUE_:
                default:
                    target.BlendEnable = False;
                    return;
            }
        }

        constexpr LayoutElement kStandardLayout[] = {
            LayoutElement{0, 0, 3, VT_FLOAT32, False}, LayoutElement{1, 0, 3, VT_FLOAT32, False},
            LayoutElement{2, 0, 4, VT_FLOAT32, False}, LayoutElement{3, 0, 2, VT_FLOAT32, False},
            LayoutElement{4, 0, 4, VT_UINT8, True},
        };

        bool CompileShader(ShaderLibrary& library, const MaterialShaderRef& ref, SHADER_TYPE type,
                           const ShaderMacroHelper& macros, const char* entry, const char* debugName,
                           RefCntAutoPtr<IShader>& outShader)
        {
            ShaderCreateInfo ci;
            ci.Desc.Name = debugName;
            ci.Desc.ShaderType = type;
            ci.Desc.UseCombinedTextureSamplers = true;
            ci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
            ci.EntryPoint = (entry != nullptr && entry[0] != '\0') ? entry : "main";
            ci.Macros = macros;
            ci.LoadConstantBufferReflection = true;

            RefCntAutoPtr<IShaderSourceInputStreamFactory> memoryFactory;
            RefCntAutoPtr<IShaderSourceInputStreamFactory> compoundFactory;
            MemoryShaderSourceFileInfo memorySource{};
            if (ref.m_bytes != nullptr && ref.m_byteCount > 0)
            {
                memorySource.Name = debugName;
                memorySource.pData = reinterpret_cast<const Char*>(ref.m_bytes);
                memorySource.Length = ref.m_byteCount;
                MemoryShaderSourceFactoryCreateInfo memoryInfo;
                memoryInfo.pSources = &memorySource;
                memoryInfo.NumSources = 1;
                memoryInfo.CopySources = true;
                CreateMemoryShaderSourceFactory(memoryInfo, &memoryFactory);
                if (!memoryFactory)
                {
                    hive::LogError(LOG_SWARM, "CreateMaterial({}): memory source factory failed", debugName);
                    return false;
                }

                // Chain memory factory with the engine library so #include directives in
                // asset-driven shaders still resolve common.hlsli and other helpers.
                IShaderSourceInputStreamFactory* libraryFactory = library.GetSourceFactory();
                IShaderSourceInputStreamFactory* factories[2] = {memoryFactory.RawPtr(), libraryFactory};
                CompoundShaderSourceFactoryCreateInfo compoundInfo;
                compoundInfo.ppFactories = factories;
                compoundInfo.NumFactories = (libraryFactory != nullptr) ? 2u : 1u;
                CreateCompoundShaderSourceFactory(compoundInfo, &compoundFactory);

                ci.FilePath = debugName;
                ci.pShaderSourceStreamFactory = compoundFactory ? compoundFactory.RawPtr() : memoryFactory.RawPtr();
                if (ref.m_entry != nullptr && ref.m_entry[0] != '\0')
                {
                    ci.EntryPoint = ref.m_entry;
                }
            }
            else
            {
                if (ref.m_path == nullptr || ref.m_path[0] == '\0')
                {
                    hive::LogError(LOG_SWARM, "CreateMaterial({}): missing shader source for stage", debugName);
                    return false;
                }
                ci.FilePath = ref.m_path;
                ci.pShaderSourceStreamFactory = library.GetSourceFactory();
            }

            library.GetCache()->CreateShader(ci, &outShader);
            if (!outShader)
            {
                hive::LogError(LOG_SWARM, "CreateMaterial({}): shader compilation failed", debugName);
                return false;
            }
            return true;
        }
    } // namespace

    Material* CreateMaterial(RenderContext* context, const MaterialDesc& desc)
    {
        using namespace Diligent;

        if (context == nullptr || context->m_device == nullptr || context->m_swapchain == nullptr)
        {
            return nullptr;
        }
        if (context->m_shaderLibrary == nullptr)
        {
            hive::LogError(LOG_SWARM, "CreateMaterial: ShaderLibrary not initialized");
            return nullptr;
        }
        if (context->m_viewConstantBuffer == nullptr || context->m_objectConstantBuffer == nullptr)
        {
            hive::LogError(LOG_SWARM, "CreateMaterial: scene constants are not initialized");
            return nullptr;
        }

        const char* debugName = (desc.m_debugName != nullptr) ? desc.m_debugName : "Material";

        ShaderMacroHelper macros;
        for (uint32_t i = 0; i < desc.m_macroCount; ++i)
        {
            if (desc.m_macros[i].m_name != nullptr)
            {
                macros.Add(desc.m_macros[i].m_name,
                           desc.m_macros[i].m_value != nullptr ? desc.m_macros[i].m_value : "1");
            }
        }

        wax::String vsName;
        vsName.Append(debugName, std::strlen(debugName));
        vsName.Append(" VS");
        wax::String psName;
        psName.Append(debugName, std::strlen(debugName));
        psName.Append(" PS");

        RefCntAutoPtr<IShader> vertexShader;
        if (!CompileShader(*context->m_shaderLibrary, desc.m_vertexShader, SHADER_TYPE_VERTEX, macros,
                           desc.m_vertexEntry, vsName.CStr(), vertexShader))
        {
            return nullptr;
        }

        RefCntAutoPtr<IShader> pixelShader;
        if (!CompileShader(*context->m_shaderLibrary, desc.m_pixelShader, SHADER_TYPE_PIXEL, macros, desc.m_pixelEntry,
                           psName.CStr(), pixelShader))
        {
            return nullptr;
        }

        wax::String psoName;
        psoName.Append(debugName, std::strlen(debugName));
        psoName.Append(" PSO");

        GraphicsPipelineStateCreateInfo psoInfo;
        psoInfo.PSODesc.Name = psoName.CStr();
        psoInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
        psoInfo.GraphicsPipeline.NumRenderTargets = 1;
        psoInfo.GraphicsPipeline.RTVFormats[0] = context->m_swapchain->GetDesc().ColorBufferFormat;
        psoInfo.GraphicsPipeline.DSVFormat = context->m_swapchain->GetDesc().DepthBufferFormat;
        psoInfo.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        psoInfo.GraphicsPipeline.RasterizerDesc.CullMode = TranslateCullMode(desc.m_cullMode);
        psoInfo.GraphicsPipeline.RasterizerDesc.FillMode = TranslateFillMode(desc.m_fillMode);
        psoInfo.GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = desc.m_frontCCW ? True : False;
        psoInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = desc.m_depthTest ? True : False;
        psoInfo.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = desc.m_depthWrite ? True : False;
        ConfigureBlend(psoInfo.GraphicsPipeline.BlendDesc.RenderTargets[0], desc.m_blendMode);
        psoInfo.GraphicsPipeline.InputLayout.LayoutElements = kStandardLayout;
        psoInfo.GraphicsPipeline.InputLayout.NumElements = _countof(kStandardLayout);
        psoInfo.pVS = vertexShader;
        psoInfo.pPS = pixelShader;

        psoInfo.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

        // The pixel shader picks one of two binding modes: bindless if it declares the
        // runtime array `g_textures`, otherwise one-texture-per-variable as before.
        bool usesBindless = false;
        if (pixelShader)
        {
            const Uint32 resCount = pixelShader->GetResourceCount();
            for (Uint32 i = 0; i < resCount; ++i)
            {
                ShaderResourceDesc rd{};
                pixelShader->GetResourceDesc(i, rd);
                if (rd.Name != nullptr && std::strcmp(rd.Name, "g_textures") == 0)
                {
                    usesBindless = true;
                    break;
                }
            }
        }

        wax::Vector<ShaderResourceVariableDesc> mutableVars{SwarmModule::GetInstance().GetAllocator()};

        wax::Vector<ImmutableSamplerDesc> immutableSamplers{SwarmModule::GetInstance().GetAllocator()};

        auto translateFilter = [](SamplerFilter f) {
            return f == SamplerFilter::NEAREST ? FILTER_TYPE_POINT : FILTER_TYPE_LINEAR;
        };
        auto translateAddress = [](SamplerAddress a) {
            return a == SamplerAddress::CLAMP ? TEXTURE_ADDRESS_CLAMP : TEXTURE_ADDRESS_WRAP;
        };

        if (usesBindless)
        {
            mutableVars.PushBack({SHADER_TYPE_PIXEL, "g_textures", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE});

            Diligent::SamplerDesc bindlessSampler;
            bindlessSampler.MinFilter = FILTER_TYPE_LINEAR;
            bindlessSampler.MagFilter = FILTER_TYPE_LINEAR;
            bindlessSampler.MipFilter = FILTER_TYPE_LINEAR;
            bindlessSampler.AddressU = TEXTURE_ADDRESS_WRAP;
            bindlessSampler.AddressV = TEXTURE_ADDRESS_WRAP;
            bindlessSampler.AddressW = TEXTURE_ADDRESS_WRAP;
            immutableSamplers.PushBack(ImmutableSamplerDesc{SHADER_TYPE_PIXEL, "g_textures", bindlessSampler});
        }
        else
        {
            for (uint32_t t = 0; t < desc.m_textureCount; ++t)
            {
                const MaterialTextureBinding& tb = desc.m_textures[t];
                if (tb.m_name == nullptr || tb.m_texture == nullptr)
                    continue;
                mutableVars.PushBack({SHADER_TYPE_PIXEL, tb.m_name, SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE});

                Diligent::SamplerDesc sd;
                const FILTER_TYPE filter = translateFilter(tb.m_sampler.m_filter);
                sd.MinFilter = filter;
                sd.MagFilter = filter;
                sd.MipFilter = tb.m_sampler.m_mipmaps ? filter : FILTER_TYPE_POINT;
                const TEXTURE_ADDRESS_MODE addr = translateAddress(tb.m_sampler.m_address);
                sd.AddressU = addr;
                sd.AddressV = addr;
                sd.AddressW = addr;
                immutableSamplers.PushBack(ImmutableSamplerDesc{SHADER_TYPE_PIXEL, tb.m_name, sd});
            }
        }

        psoInfo.PSODesc.ResourceLayout.Variables = mutableVars.Data();
        psoInfo.PSODesc.ResourceLayout.NumVariables = static_cast<Uint32>(mutableVars.Size());
        psoInfo.PSODesc.ResourceLayout.ImmutableSamplers = immutableSamplers.Data();
        psoInfo.PSODesc.ResourceLayout.NumImmutableSamplers = static_cast<Uint32>(immutableSamplers.Size());

        RefCntAutoPtr<IPipelineState> pipelineState;
        context->m_shaderLibrary->GetCache()->CreateGraphicsPipelineState(psoInfo, &pipelineState);
        if (!pipelineState)
        {
            hive::LogError(LOG_SWARM, "CreateMaterial({}): pipeline state creation failed", debugName);
            return nullptr;
        }

        if (auto* var = pipelineState->GetStaticVariableByName(SHADER_TYPE_VERTEX, "ViewConstants"))
        {
            var->Set(context->m_viewConstantBuffer);
        }
        if (auto* var = pipelineState->GetStaticVariableByName(SHADER_TYPE_VERTEX, "ObjectConstants"))
        {
            var->Set(context->m_objectConstantBuffer);
        }
        if (auto* var = pipelineState->GetStaticVariableByName(SHADER_TYPE_PIXEL, "ObjectConstants"))
        {
            var->Set(context->m_objectConstantBuffer);
        }
        if (auto* var = pipelineState->GetStaticVariableByName(SHADER_TYPE_VERTEX, "TimeConstants"))
        {
            var->Set(context->m_timeConstantBuffer);
        }
        if (auto* var = pipelineState->GetStaticVariableByName(SHADER_TYPE_PIXEL, "ViewConstants"))
        {
            var->Set(context->m_viewConstantBuffer);
        }
        if (auto* var = pipelineState->GetStaticVariableByName(SHADER_TYPE_PIXEL, "TimeConstants"))
        {
            var->Set(context->m_timeConstantBuffer);
        }
        if (auto* var = pipelineState->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_materials"))
        {
            var->Set(context->m_materialsBuffer->ShaderView());
        }

        RefCntAutoPtr<IShaderResourceBinding> srb;
        pipelineState->CreateShaderResourceBinding(&srb, true);
        if (!srb)
        {
            hive::LogError(LOG_SWARM, "CreateMaterial({}): SRB creation failed", debugName);
            return nullptr;
        }

        // Hardcoded standard material struct layout. Mirrors MaterialParams in standard.ps.hlsl
        // and MaterialParamsGpu in diligent_materials_buffer.h. Other shader programs would
        // register their own layouts here when they ship.
        struct StandardField
        {
            const char* m_name;
            uint32_t m_offset;
            uint32_t m_size;
        };
        static constexpr StandardField kStandardLayout[] = {
            {"base_color_factor", offsetof(MaterialParamsGpu, m_baseColorFactor), 16},
            {"metallic_factor", offsetof(MaterialParamsGpu, m_metallicFactor), 4},
            {"roughness_factor", offsetof(MaterialParamsGpu, m_roughnessFactor), 4},
            {"emissive_factor", offsetof(MaterialParamsGpu, m_emissiveFactor), 16},
            {"albedo_map_index", offsetof(MaterialParamsGpu, m_albedoMapIndex), 4},
            {"normal_map_index", offsetof(MaterialParamsGpu, m_normalMapIndex), 4},
            {"metallic_roughness_map_index", offsetof(MaterialParamsGpu, m_metallicRoughnessMapIndex), 4},
        };

        MaterialParamsGpu params{};
        params.m_baseColorFactor[0] = 1.f;
        params.m_baseColorFactor[1] = 1.f;
        params.m_baseColorFactor[2] = 1.f;
        params.m_baseColorFactor[3] = 1.f;
        params.m_metallicFactor = 0.f;
        params.m_roughnessFactor = 1.f;
        params.m_albedoMapIndex = kBindlessSlotDefaultWhite;
        params.m_normalMapIndex = kBindlessSlotDefaultNormal;
        params.m_metallicRoughnessMapIndex = kBindlessSlotDefaultWhite;

        for (uint32_t p = 0; p < desc.m_paramCount; ++p)
        {
            const MaterialParamBinding& binding = desc.m_params[p];
            if (binding.m_name == nullptr || binding.m_data == nullptr)
                continue;
            for (const StandardField& f : kStandardLayout)
            {
                if (std::strcmp(f.m_name, binding.m_name) != 0)
                    continue;
                const uint32_t copySize = (binding.m_size < f.m_size) ? binding.m_size : f.m_size;
                std::memcpy(reinterpret_cast<uint8_t*>(&params) + f.m_offset, binding.m_data, copySize);
                break;
            }
        }

        const uint32_t materialIndex = context->m_materialsBuffer->Allocate();
        context->m_materialsBuffer->Write(context->m_context, materialIndex, &params, sizeof(params));

        if (usesBindless)
        {
            BindlessHeap* heap = context->m_bindlessHeap;
            if (auto* var = srb->GetVariableByName(SHADER_TYPE_PIXEL, "g_textures"))
            {
                // Every slot must resolve to a non-null view for validation. Empty slots fall
                // back to the engine default white texture; the shader only samples slots its
                // material actually populated.
                Texture* fallback = heap->Lookup(TextureHandle{kBindlessSlotDefaultWhite});
                ITextureView* fallbackView =
                    (fallback != nullptr) ? fallback->m_shaderView : nullptr;

                const uint32_t capacity = heap->Capacity();
                wax::Vector<IDeviceObject*> views{SwarmModule::GetInstance().GetAllocator()};
                views.Resize(capacity);
                for (uint32_t i = 0; i < capacity; ++i)
                {
                    Texture* tex = heap->Lookup(TextureHandle{i});
                    ITextureView* view =
                        (tex != nullptr && tex->m_shaderView != nullptr) ? tex->m_shaderView : fallbackView;
                    views[i] = static_cast<IDeviceObject*>(view);
                }
                var->SetArray(views.Data(), 0, capacity, SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
            }
        }
        else
        {
            for (uint32_t t = 0; t < desc.m_textureCount; ++t)
            {
                const MaterialTextureBinding& tb = desc.m_textures[t];
                if (tb.m_name == nullptr || tb.m_texture == nullptr || tb.m_texture->m_shaderView == nullptr)
                    continue;
                if (auto* var = srb->GetVariableByName(SHADER_TYPE_PIXEL, tb.m_name))
                    var->Set(tb.m_texture->m_shaderView);
            }
        }

        auto& allocator = SwarmModule::GetInstance().GetAllocator();
        auto* material = comb::New<Material>(allocator);
        material->m_pipelineState = pipelineState.Detach();
        material->m_resourceBinding = srb.Detach();
        material->m_materialIndex = materialIndex;
        material->m_domain = desc.m_domain;
        return material;
    }

    void DestroyMaterial(RenderContext* context, Material* material)
    {
        if (material == nullptr)
        {
            return;
        }

        context->m_materialsBuffer->Free(material->m_materialIndex);

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
