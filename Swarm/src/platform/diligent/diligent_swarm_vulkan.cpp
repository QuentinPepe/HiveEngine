#include <swarm/platform/diligent_swarm.h>
#include <swarm/precomp.h>

#include <EngineFactoryVk.h>

#include <thread>

namespace swarm
{
    namespace
    {
        uint32_t ChooseDeferredContextCount()
        {
            const uint32_t hw = std::thread::hardware_concurrency();
            // hardware_concurrency may report 0 on exotic platforms; clamp to a sane band so we
            // always create at least one deferred ctx (used by the single-thread render path).
            const uint32_t desired = hw == 0 ? 4 : hw;
            return desired < 1 ? 1 : (desired > kMaxDeferredContexts ? kMaxDeferredContexts : desired);
        }
    } // namespace

    bool InitRenderContextCommon(RenderContext* renderContext)
    {
        auto* factory = Diligent::GetEngineFactoryVk();

        renderContext->m_deferredContextCount = ChooseDeferredContextCount();

        Diligent::EngineVkCreateInfo createInfo{};
        createInfo.NumDeferredContexts = renderContext->m_deferredContextCount;

        // Diligent returns immediate ctx at index 0, deferred contexts from index 1.
        Diligent::IDeviceContext* contexts[1 + kMaxDeferredContexts]{};
        factory->CreateDeviceAndContextsVk(createInfo, &renderContext->m_device, contexts);
        renderContext->m_context = contexts[0];
        for (uint32_t i = 0; i < renderContext->m_deferredContextCount; ++i)
        {
            renderContext->m_deferredContexts[i] = contexts[1 + i];
            renderContext->m_viewDirty[i] = true;
        }

        return renderContext->m_context != nullptr;
    }
} // namespace swarm
