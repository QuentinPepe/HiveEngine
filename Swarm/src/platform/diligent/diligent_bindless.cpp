#include <diligent_bindless.h>

#include <swarm/swarm_log.h>
#include <swarm/swarmmodule.h>

#include <hive/core/log.h>

namespace swarm
{
    BindlessHeap::BindlessHeap()
        : m_freeList{SwarmModule::GetInstance().GetAllocator()}
    {
    }

    void BindlessHeap::SetDefault(uint32_t slot, Texture* texture)
    {
        if (slot != kBindlessSlotDefaultWhite && slot != kBindlessSlotDefaultNormal)
        {
            return;
        }
        if (m_slots[slot] == nullptr && texture != nullptr)
        {
            ++m_liveCount;
        }
        else if (m_slots[slot] != nullptr && texture == nullptr)
        {
            --m_liveCount;
        }
        m_slots[slot] = texture;
    }

    TextureHandle BindlessHeap::Register(Texture* texture)
    {
        if (texture == nullptr)
        {
            return TextureHandle{};
        }
        uint32_t slot = 0;
        if (!m_freeList.IsEmpty())
        {
            slot = m_freeList.Back();
            m_freeList.PopBack();
        }
        else if (m_nextSlot < kBindlessHeapCapacity)
        {
            slot = m_nextSlot++;
        }
        else
        {
            hive::LogError(LOG_SWARM, "BindlessHeap full (capacity={})", kBindlessHeapCapacity);
            return TextureHandle{};
        }
        m_slots[slot] = texture;
        ++m_liveCount;
        return TextureHandle{slot};
    }

    void BindlessHeap::Unregister(TextureHandle handle)
    {
        // Defaults are owned by the render context — never released through this path.
        if (!handle.IsValid() || handle.m_index == kBindlessSlotDefaultWhite ||
            handle.m_index == kBindlessSlotDefaultNormal)
        {
            return;
        }
        m_slots[handle.m_index] = nullptr;
        m_freeList.PushBack(handle.m_index);
        --m_liveCount;
    }

    Texture* BindlessHeap::Lookup(TextureHandle handle) const
    {
        return m_slots[handle.m_index];
    }
} // namespace swarm
