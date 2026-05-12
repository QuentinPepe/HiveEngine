#include <diligent_materials_buffer.h>

#include <swarm/swarm_log.h>
#include <swarm/swarmmodule.h>

#include <hive/core/log.h>

#include <GraphicsTypes.h>

namespace swarm
{
    using namespace Diligent;

    MaterialsBuffer::MaterialsBuffer()
        : m_freeList{SwarmModule::GetInstance().GetAllocator()}
    {
    }

    bool MaterialsBuffer::Initialize(IRenderDevice* device)
    {
        BufferDesc bd;
        bd.Name = "Swarm MaterialParams";
        bd.Size = static_cast<Uint64>(sizeof(MaterialParamsGpu)) * kMaterialsSlabCapacity;
        bd.Usage = USAGE_DEFAULT;
        bd.BindFlags = BIND_SHADER_RESOURCE;
        bd.Mode = BUFFER_MODE_STRUCTURED;
        bd.ElementByteStride = sizeof(MaterialParamsGpu);

        device->CreateBuffer(bd, nullptr, &m_buffer);
        if (!m_buffer)
        {
            hive::LogError(LOG_SWARM, "MaterialsBuffer: failed to create global buffer");
            return false;
        }

        m_view = m_buffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE);
        return m_view != nullptr;
    }

    void MaterialsBuffer::Shutdown()
    {
        m_view = nullptr;
        m_buffer.Release();
        m_freeList.Clear();
        m_nextSlot = 0;
    }

    uint32_t MaterialsBuffer::Allocate()
    {
        if (!m_freeList.IsEmpty())
        {
            const uint32_t slot = m_freeList.Back();
            m_freeList.PopBack();
            return slot;
        }
        if (m_nextSlot < kMaterialsSlabCapacity)
        {
            return m_nextSlot++;
        }
        hive::LogError(LOG_SWARM, "MaterialsBuffer full (capacity={})", kMaterialsSlabCapacity);
        return 0;
    }

    void MaterialsBuffer::Free(uint32_t slot)
    {
        if (slot >= kMaterialsSlabCapacity)
        {
            return;
        }
        m_freeList.PushBack(slot);
    }

    void MaterialsBuffer::Write(IDeviceContext* immediate, uint32_t slot, const void* data, uint32_t size)
    {
        const Uint64 offset = static_cast<Uint64>(slot) * sizeof(MaterialParamsGpu);
        immediate->UpdateBuffer(m_buffer, offset, size, data, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
} // namespace swarm
