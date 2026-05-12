#pragma once

#include <wax/containers/vector.h>

#include <Buffer.h>
#include <BufferView.h>
#include <RefCntAutoPtr.hpp>
#include <RenderDevice.h>

#include <cstdint>

namespace swarm
{
    inline constexpr uint32_t kMaterialsSlabCapacity = 1024;

    struct MaterialParamsGpu
    {
        float m_baseColorFactor[4];
        float m_metallicFactor;
        float m_roughnessFactor;
        float m_pad0[2];
        float m_emissiveFactor[4];
        uint32_t m_albedoMapIndex;
        uint32_t m_normalMapIndex;
        uint32_t m_metallicRoughnessMapIndex;
        uint32_t m_padIndices;
    };
    static_assert(sizeof(MaterialParamsGpu) == 64, "MaterialParamsGpu must be 64 bytes");

    class MaterialsBuffer
    {
    public:
        MaterialsBuffer();

        bool Initialize(Diligent::IRenderDevice* device);
        void Shutdown();

        [[nodiscard]] uint32_t Allocate();
        void Free(uint32_t slot);
        void Write(Diligent::IDeviceContext* immediate, uint32_t slot, const void* data, uint32_t size);

        [[nodiscard]] Diligent::IBufferView* ShaderView() const noexcept
        {
            return m_view;
        }

    private:
        Diligent::RefCntAutoPtr<Diligent::IBuffer> m_buffer;
        Diligent::IBufferView* m_view{nullptr};
        wax::Vector<uint32_t> m_freeList;
        uint32_t m_nextSlot{0};
    };
} // namespace swarm
