#include <waggle/scene/scene_io.h>

#include <hive/core/log.h>
#include <hive/math/transforms.h>

#include <comb/default_allocator.h>

#include <wax/containers/vector.h>

#include <queen/reflect/world_deserializer.h>
#include <queen/reflect/world_serializer.h>
#include <queen/world/world.h>

#include <propolis/runtime/propolis_script.h>

#include <waggle/components/camera.h>
#include <waggle/components/editor_only.h>
#include <waggle/components/lighting.h>
#include <waggle/components/mesh_reference.h>
#include <waggle/components/name.h>
#include <waggle/components/transform.h>
#include <waggle/time.h>

#include <cstdio>
#include <cstring>

namespace waggle
{
    namespace
    {
        const hive::LogCategory LOG_SCENE{"Waggle.Scene"};
    }

    void RegisterEngineSceneComponentTypes(queen::ComponentRegistry<256>& registry)
    {
        if (!registry.Contains<Transform>())
        {
            registry.Register<Transform>();
        }
        if (!registry.Contains<Camera>())
        {
            registry.Register<Camera>();
        }
        if (!registry.Contains<DirectionalLight>())
        {
            registry.Register<DirectionalLight>();
        }
        if (!registry.Contains<AmbientLight>())
        {
            registry.Register<AmbientLight>();
        }
        if (!registry.Contains<Name>())
        {
            registry.Register<Name>();
        }
        if (!registry.Contains<MeshReference>())
        {
            registry.Register<MeshReference>();
        }
        if (!registry.Contains<propolis::PropolisScript>())
        {
            registry.Register<propolis::PropolisScript>();
        }
    }

    bool SaveScene(queen::World& world, const queen::ComponentRegistry<256>& registry, const char* path)
    {
        queen::DynamicWorldSerializer serializer{};
        serializer.SkipArchetypesWith<EditorOnly>();
        auto result = serializer.Serialize(world, registry);
        if (!result.m_success)
        {
            hive::LogError(LOG_SCENE, "Failed to serialize scene");
            return false;
        }

        FILE* f = nullptr;
#ifdef _MSC_VER
        fopen_s(&f, path, "w");
#else
        f = std::fopen(path, "w");
#endif
        if (f == nullptr)
        {
            hive::LogError(LOG_SCENE, "Failed to open file for writing: {}", path);
            return false;
        }

        std::fwrite(serializer.CStr(), 1, serializer.Size(), f);
        std::fclose(f);

        hive::LogInfo(LOG_SCENE, "Scene saved: {} ({} entities, {} components)",
                      path, result.m_entitiesWritten, result.m_componentsWritten);
        return true;
    }

    bool LoadScene(queen::World& world, const queen::ComponentRegistry<256>& registry, const char* path)
    {
        FILE* f = nullptr;
#ifdef _MSC_VER
        fopen_s(&f, path, "rb");
#else
        f = std::fopen(path, "rb");
#endif
        if (f == nullptr)
        {
            hive::LogError(LOG_SCENE, "Failed to open scene file: {}", path);
            return false;
        }

        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (size <= 0)
        {
            std::fclose(f);
            hive::LogError(LOG_SCENE, "Empty scene file: {}", path);
            return false;
        }

        wax::Vector<char> buffer{comb::GetDefaultAllocator()};
        buffer.Resize(static_cast<size_t>(size) + 1, '\0');
        std::fread(buffer.Data(), 1, static_cast<size_t>(size), f);
        std::fclose(f);

        auto result = queen::WorldDeserializer::Deserialize(world, registry, buffer.Data());
        if (!result.m_success)
        {
            hive::LogError(LOG_SCENE, "Failed to deserialize scene: {}",
                           result.m_error != nullptr ? result.m_error : "unknown");
            return false;
        }

        EnsureRuntimeTransformComponents(world);

        hive::LogInfo(LOG_SCENE, "Scene loaded: {} ({} entities, {} components, {} skipped)",
                      path, result.m_entitiesLoaded, result.m_componentsLoaded, result.m_componentsSkipped);
        return true;
    }

    bool LoadSceneFromMemory(queen::World& world, const queen::ComponentRegistry<256>& registry,
                             const char* jsonData, size_t jsonSize)
    {
        if (jsonData == nullptr || jsonSize == 0)
        {
            hive::LogError(LOG_SCENE, "LoadSceneFromMemory: empty buffer");
            return false;
        }

        wax::Vector<char> buffer{comb::GetDefaultAllocator()};
        buffer.Resize(jsonSize + 1, '\0');
        std::memcpy(buffer.Data(), jsonData, jsonSize);

        auto result = queen::WorldDeserializer::Deserialize(world, registry, buffer.Data());
        if (!result.m_success)
        {
            hive::LogError(LOG_SCENE, "Failed to deserialize scene: {}",
                           result.m_error != nullptr ? result.m_error : "unknown");
            return false;
        }

        EnsureRuntimeTransformComponents(world);

        hive::LogInfo(LOG_SCENE, "Scene loaded from memory ({} bytes, {} entities, {} components, {} skipped)",
                      jsonSize, result.m_entitiesLoaded, result.m_componentsLoaded, result.m_componentsSkipped);
        return true;
    }

    void EnsureRuntimeTransformComponents(queen::World& world)
    {
        wax::Vector<queen::Entity> needWorldMatrix{comb::GetDefaultAllocator()};
        world.ForEachArchetype([&](auto& archetype) {
            if (!archetype.template HasComponent<Transform>())
            {
                return;
            }
            if (archetype.template HasComponent<WorldMatrix>())
            {
                return;
            }
            for (uint32_t row = 0; row < archetype.EntityCount(); ++row)
            {
                needWorldMatrix.PushBack(archetype.GetEntity(row));
            }
        });

        const auto* time = world.Resource<Time>();
        const uint64_t dirtyTick = (time != nullptr) ? time->m_tick + 1 : 1;

        for (size_t i = 0; i < needWorldMatrix.Size(); ++i)
        {
            const queen::Entity entity = needWorldMatrix[i];
            auto* transform = world.Get<Transform>(entity);
            const hive::math::Mat4 matrix =
                (transform != nullptr)
                    ? hive::math::TRS(transform->m_position, transform->m_rotation, transform->m_scale)
                    : hive::math::Mat4::Identity();
            world.Add(entity, WorldMatrix{matrix});
            world.Add(entity, TransformVersion{dirtyTick});
        }

        // Existing Transform-bearing entities also need their hierarchy re-resolved so the
        // next transform_system pass propagates parent scales/positions to children.
        world.Query<queen::Write<TransformVersion>, queen::Read<Transform>>().Each(
            [&](TransformVersion& version, const Transform&) {
                version.m_lastModified = dirtyTick;
            });
    }
}
