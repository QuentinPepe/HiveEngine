#include <forge/scene_file.h>

#include <hive/core/log.h>
#include <hive/math/transforms.h>

#include <queen/reflect/world_deserializer.h>
#include <queen/reflect/world_serializer.h>
#include <queen/world/world.h>

#include <waggle/components/editor_only.h>
#include <waggle/components/transform.h>
#include <waggle/time.h>

#include <forge/forge_module.h>

#include <wax/containers/vector.h>

#include <cstdio>

static const hive::LogCategory LOG_FORGE{"Forge"};

namespace forge
{
    bool SaveScene(queen::World& world, const queen::ComponentRegistry<256>& registry, const char* path)
    {
        queen::DynamicWorldSerializer serializer{};
        serializer.SkipArchetypesWith<waggle::EditorOnly>();
        auto result = serializer.Serialize(world, registry);
        if (!result.m_success)
        {
            hive::LogError(LOG_FORGE, "Failed to serialize scene");
            return false;
        }

        FILE* f = nullptr;
#ifdef _MSC_VER
        fopen_s(&f, path, "w");
#else
        f = fopen(path, "w");
#endif
        if (!f)
        {
            hive::LogError(LOG_FORGE, "Failed to open file for writing: {}", path);
            return false;
        }

        fwrite(serializer.CStr(), 1, serializer.Size(), f);
        fclose(f);

        hive::LogInfo(LOG_FORGE, "Scene saved: {} ({} entities, {} components)", path, result.m_entitiesWritten,
                      result.m_componentsWritten);
        return true;
    }

    bool LoadScene(queen::World& world, const queen::ComponentRegistry<256>& registry, const char* path)
    {
        FILE* f = nullptr;
#ifdef _MSC_VER
        fopen_s(&f, path, "r");
#else
        f = fopen(path, "r");
#endif
        if (!f)
        {
            hive::LogError(LOG_FORGE, "Failed to open scene file: {}", path);
            return false;
        }

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        wax::Vector<char> buffer{forge::GetAllocator()};
        buffer.Resize(static_cast<size_t>(size) + 1, '\0');
        fread(buffer.Data(), 1, static_cast<size_t>(size), f);
        fclose(f);

        auto result = queen::WorldDeserializer::Deserialize(world, registry, buffer.Data());
        if (!result.m_success)
        {
            hive::LogError(LOG_FORGE, "Failed to deserialize scene: {}", result.m_error ? result.m_error : "unknown");
            return false;
        }

        EnsureRuntimeTransformComponents(world);

        hive::LogInfo(LOG_FORGE, "Scene loaded: {} ({} entities, {} components, {} skipped)",
                      path, result.m_entitiesLoaded, result.m_componentsLoaded, result.m_componentsSkipped);
        return true;
    }

    void EnsureRuntimeTransformComponents(queen::World& world)
    {
        wax::Vector<queen::Entity> needWorldMatrix{forge::GetAllocator()};
        world.ForEachArchetype([&](auto& archetype) {
            if (!archetype.template HasComponent<waggle::Transform>())
            {
                return;
            }
            if (archetype.template HasComponent<waggle::WorldMatrix>())
            {
                return;
            }
            for (uint32_t row = 0; row < archetype.EntityCount(); ++row)
            {
                needWorldMatrix.PushBack(archetype.GetEntity(row));
            }
        });

        const auto* time = world.Resource<waggle::Time>();
        const uint64_t dirtyTick = time != nullptr ? time->m_tick + 1 : 1;

        for (queen::Entity entity : needWorldMatrix)
        {
            auto* transform = world.Get<waggle::Transform>(entity);
            const hive::math::Mat4 matrix = transform != nullptr
                                                ? hive::math::TRS(transform->m_position,
                                                                  transform->m_rotation,
                                                                  transform->m_scale)
                                                : hive::math::Mat4::Identity();
            world.Add(entity, waggle::WorldMatrix{matrix});
            world.Add(entity, waggle::TransformVersion{dirtyTick});
        }

        // Existing Transform-bearing entities also need their hierarchy re-resolved so the
        // next transform_system pass propagates parent scales/positions to children.
        world.Query<queen::Write<waggle::TransformVersion>, queen::Read<waggle::Transform>>().Each(
            [&](waggle::TransformVersion& version, const waggle::Transform&) {
                version.m_lastModified = dirtyTick;
            });
    }
} // namespace forge
