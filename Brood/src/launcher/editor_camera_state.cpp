#include <launcher/editor_camera_state.h>

#if HIVE_MODE_EDITOR

#include <brood/launcher/launcher_types.h>

#include <hive/math/types.h>

#include <queen/query/query_term.h>
#include <queen/reflect/component_reflector.h>
#include <queen/reflect/json_deserializer.h>
#include <queen/reflect/json_serializer.h>
#include <queen/reflect/reflectable.h>
#include <queen/world/world.h>

#include <waggle/components/editor_camera.h>
#include <waggle/components/editor_only.h>
#include <waggle/components/transform.h>
#include <waggle/project/project_manager.h>

#include <cstdio>
#include <string>
#include <system_error>

namespace brood::launcher
{
    namespace
    {
        struct EditorCameraStateData
        {
            hive::math::Float3 m_position{0.f, 0.f, 0.f};
            float m_yaw{0.f};
            float m_pitch{0.f};
            float m_moveSpeed{1.f};

            static void Reflect(queen::ComponentReflector<>& r)
            {
                r.Field("position", &EditorCameraStateData::m_position);
                r.Field("yaw", &EditorCameraStateData::m_yaw);
                r.Field("pitch", &EditorCameraStateData::m_pitch);
                r.Field("move_speed", &EditorCameraStateData::m_moveSpeed);
            }
        };

        std::string SanitizeRelative(const std::filesystem::path& assetsDir,
                                     const std::filesystem::path& scenePath)
        {
            std::error_code ec;
            std::filesystem::path rel = std::filesystem::relative(scenePath, assetsDir, ec);
            if (ec || rel.empty())
                return {};
            std::string out = rel.generic_string();
            for (auto& ch : out)
            {
                if (ch == '/' || ch == '\\' || ch == ':')
                    ch = '_';
            }
            return out;
        }

        bool WriteAllBytes(const std::filesystem::path& path, const char* data, size_t size)
        {
            FILE* file = nullptr;
#ifdef _MSC_VER
            fopen_s(&file, path.string().c_str(), "wb");
#else
            file = std::fopen(path.string().c_str(), "wb");
#endif
            if (file == nullptr)
                return false;
            const size_t written = std::fwrite(data, 1, size, file);
            std::fclose(file);
            return written == size;
        }

        bool ReadAllBytes(const std::filesystem::path& path, std::string* out)
        {
            FILE* file = nullptr;
#ifdef _MSC_VER
            fopen_s(&file, path.string().c_str(), "rb");
#else
            file = std::fopen(path.string().c_str(), "rb");
#endif
            if (file == nullptr)
                return false;
            std::fseek(file, 0, SEEK_END);
            const long size = std::ftell(file);
            std::fseek(file, 0, SEEK_SET);
            if (size <= 0)
            {
                std::fclose(file);
                return false;
            }
            out->resize(static_cast<size_t>(size));
            const size_t bytesRead = std::fread(out->data(), 1, static_cast<size_t>(size), file);
            std::fclose(file);
            return bytesRead == static_cast<size_t>(size);
        }
    } // namespace

    std::filesystem::path GetEditorCameraStatePath(const LauncherState& state,
                                                   const std::filesystem::path& scenePath)
    {
        if (state.m_project == nullptr || scenePath.empty())
            return {};

        const std::filesystem::path assetsDir{state.m_project->Paths().m_assets.CStr()};
        const std::string safeName = SanitizeRelative(assetsDir, scenePath);
        if (safeName.empty())
            return {};

        const std::filesystem::path projectRoot{state.m_project->Paths().m_root.CStr()};
        return projectRoot / ".hive" / "editor_state" / (safeName + ".json");
    }

    bool SaveEditorCameraState(queen::World& world, const LauncherState& state,
                               const std::filesystem::path& scenePath)
    {
        const auto outputPath = GetEditorCameraStatePath(state, scenePath);
        if (outputPath.empty())
            return false;

        EditorCameraStateData snapshot{};
        bool found = false;
        world.Query<queen::Read<waggle::Transform>, queen::Read<waggle::EditorCameraController>,
                    queen::Read<waggle::EditorOnly>>()
            .Each([&](const waggle::Transform& transform, const waggle::EditorCameraController& controller,
                      const waggle::EditorOnly&) {
                if (found)
                    return;
                snapshot.m_position = transform.m_position;
                snapshot.m_yaw = controller.m_yawRad;
                snapshot.m_pitch = controller.m_pitchRad;
                snapshot.m_moveSpeed = controller.m_moveSpeed;
                found = true;
            });
        if (!found)
            return false;

        std::error_code ec;
        std::filesystem::create_directories(outputPath.parent_path(), ec);
        if (ec)
            return false;

        queen::JsonSerializer<256> serializer{};
        serializer.SerializeComponent(&snapshot, queen::GetReflectionData<EditorCameraStateData>());
        return WriteAllBytes(outputPath, serializer.CStr(), serializer.Size());
    }

    bool LoadEditorCameraState(queen::World& world, const LauncherState& state,
                               const std::filesystem::path& scenePath)
    {
        const auto path = GetEditorCameraStatePath(state, scenePath);
        if (path.empty())
            return false;

        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || ec)
            return false;

        std::string contents;
        if (!ReadAllBytes(path, &contents))
            return false;

        EditorCameraStateData loaded{};
        const auto result = queen::JsonDeserializer::DeserializeComponent(
            &loaded, queen::GetReflectionData<EditorCameraStateData>(), contents.c_str());
        if (!result.m_success)
            return false;

        bool applied = false;
        world.Query<queen::Write<waggle::Transform>, queen::Write<waggle::EditorCameraController>,
                    queen::Read<waggle::EditorOnly>>()
            .Each([&](waggle::Transform& transform, waggle::EditorCameraController& controller,
                      const waggle::EditorOnly&) {
                if (applied)
                    return;
                transform.m_position = loaded.m_position;
                controller.m_yawRad = loaded.m_yaw;
                controller.m_pitchRad = loaded.m_pitch;
                controller.m_moveSpeed = loaded.m_moveSpeed;
                applied = true;
            });
        return applied;
    }

    void RenameEditorCameraState(const LauncherState& state, const std::filesystem::path& oldScenePath,
                                 const std::filesystem::path& newScenePath)
    {
        const auto oldPath = GetEditorCameraStatePath(state, oldScenePath);
        const auto newPath = GetEditorCameraStatePath(state, newScenePath);
        if (oldPath.empty() || newPath.empty() || oldPath == newPath)
            return;

        std::error_code ec;
        if (!std::filesystem::exists(oldPath, ec))
            return;

        std::filesystem::create_directories(newPath.parent_path(), ec);
        std::filesystem::rename(oldPath, newPath, ec);
    }

    void DeleteEditorCameraState(const LauncherState& state, const std::filesystem::path& scenePath)
    {
        const auto path = GetEditorCameraStatePath(state, scenePath);
        if (path.empty())
            return;
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
} // namespace brood::launcher

#endif
