#pragma once

#include <swarm/swarm.h>

#include <wax/containers/vector.h>

#include <cstdint>

namespace swarm
{
    struct Texture;

    class BindlessHeap
    {
    public:
        BindlessHeap();

        // Engine assigns the default textures to their reserved slots once they exist.
        void SetDefault(uint32_t slot, Texture* texture);

        [[nodiscard]] TextureHandle Register(Texture* texture);
        void Unregister(TextureHandle handle);

        [[nodiscard]] Texture* Lookup(TextureHandle handle) const;
        [[nodiscard]] uint32_t Capacity() const noexcept
        {
            return kBindlessHeapCapacity;
        }
        [[nodiscard]] uint32_t LiveCount() const noexcept
        {
            return m_liveCount;
        }

    private:
        Texture* m_slots[kBindlessHeapCapacity]{};
        wax::Vector<uint32_t> m_freeList;
        uint32_t m_nextSlot{kBindlessSlotDefaultNormal + 1};
        uint32_t m_liveCount{0};
    };
} // namespace swarm
