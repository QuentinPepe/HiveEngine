#include <waggle/project/ship_pipeline.h>

#include <hive/core/log.h>

#include <wax/containers/string.h>
#include <wax/containers/vector.h>

#include <nectar/pipeline/cook_pipeline.h>
#include <nectar/project/project_file.h>

#include <waggle/project/cmake_build.h>
#include <waggle/project/project_manager.h>
#include <waggle/project/ship_pak_writer.h>
#include <waggle/scene/asset_reachability.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace waggle
{
    namespace
    {
        const hive::LogCategory LOG_SHIP{"Waggle.Ship"};

        struct CallbackBundle
        {
            ShipEventFn m_callback;
            void* m_userdata;
        };

        void EmitStage(ShipEventFn callback, void* userdata, ShipStage stage, const char* message, bool isError = false)
        {
            if (callback == nullptr)
            {
                return;
            }
            ShipEvent event{};
            event.m_stage = stage;
            event.m_message = message;
            event.m_isError = isError;
            callback(event, userdata);
        }

        void EmitProgress(ShipEventFn callback, void* userdata, ShipStage stage, uint32_t current, uint32_t total,
                          const char* message)
        {
            if (callback == nullptr)
            {
                return;
            }
            ShipEvent event{};
            event.m_stage = stage;
            event.m_current = current;
            event.m_total = total;
            event.m_message = message;
            callback(event, userdata);
        }

        void CmakeLineCallback(const char* line, bool isError, void* userdata)
        {
            auto* bundle = static_cast<CallbackBundle*>(userdata);
            if (bundle->m_callback != nullptr)
            {
                ShipEvent event{};
                event.m_stage = ShipStage::BUILDING_GAMEPLAY;
                event.m_message = line;
                event.m_isError = isError;
                bundle->m_callback(event, bundle->m_userdata);
            }
        }

        void PakProgressCallback(uint32_t current, uint32_t total, const char* entryName, void* userdata)
        {
            auto* bundle = static_cast<CallbackBundle*>(userdata);
            EmitProgress(bundle->m_callback, bundle->m_userdata, ShipStage::WRITING_PAK, current, total, entryName);
        }

        bool CopyFileSafe(const std::filesystem::path& src, const std::filesystem::path& dst, ShipEventFn callback,
                          void* userdata)
        {
            std::error_code ec;
            std::filesystem::create_directories(dst.parent_path(), ec);
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec)
            {
                const std::string msg = std::string{"copy failed: "} + src.generic_string() + " -> " +
                                        dst.generic_string() + ": " + ec.message();
                EmitStage(callback, userdata, ShipStage::COPYING_RUNTIME, msg.c_str(), true);
                return false;
            }
            return true;
        }

        std::filesystem::path FindEngineLauncher(const ShipRequest& request)
        {
            std::string config{request.m_config.Data(), request.m_config.Size()};
            if (config.empty())
            {
                config = "Retail";
            }
            std::string presetLower;
            presetLower.reserve(config.size());
            for (char c : config)
            {
                presetLower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            const std::string presetName = "hive-game-" + presetLower;

            auto tryRoot = [&](const std::filesystem::path& root) -> std::filesystem::path {
                std::filesystem::path candidate = root / "out" / "build" / presetName / "bin" / config / "hive_launcher.exe";
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec))
                {
                    return candidate;
                }
                return {};
            };

            if (!request.m_engineRoot.IsEmpty())
            {
                std::filesystem::path root{std::string{request.m_engineRoot.Data(), request.m_engineRoot.Size()}};
                auto result = tryRoot(root);
                if (!result.empty())
                {
                    return result;
                }
            }
            const char* env = std::getenv("HIVE_ENGINE_DIR");
            if (env != nullptr)
            {
                auto result = tryRoot(std::filesystem::path{env});
                if (!result.empty())
                {
                    return result;
                }
            }
            return {};
        }
    } // namespace

    ShipPipeline::ShipPipeline(comb::DefaultAllocator& alloc, ProjectManager& project,
                               queen::ComponentRegistry<256>& registry)
        : m_alloc{&alloc}
        , m_project{&project}
        , m_registry{&registry}
    {
    }

    bool ShipPipeline::Run(const ShipRequest& request, ShipEventFn callback, void* userdata)
    {
        if (request.m_outputDir.IsEmpty())
        {
            EmitStage(callback, userdata, ShipStage::FAILED, "Output dir is empty", true);
            return false;
        }
        if (!m_project->IsOpen())
        {
            EmitStage(callback, userdata, ShipStage::FAILED, "Project is not open", true);
            return false;
        }
        if (m_project->IsShipped())
        {
            EmitStage(callback, userdata, ShipStage::FAILED, "Cannot ship from a shipped project", true);
            return false;
        }

        const std::string outputDirStr{request.m_outputDir.Data(), request.m_outputDir.Size()};
        const std::filesystem::path outputDir{outputDirStr};
        std::error_code ec;
        std::filesystem::create_directories(outputDir, ec);
        if (ec)
        {
            EmitStage(callback, userdata, ShipStage::FAILED, ec.message().c_str(), true);
            return false;
        }

        const std::string projectRoot{m_project->Paths().m_root.View().Data(), m_project->Paths().m_root.View().Size()};

        const std::filesystem::path engineLauncherEarly = FindEngineLauncher(request);
        const std::filesystem::path engineBuildDir =
            engineLauncherEarly.empty() ? std::filesystem::path{}
                                        : engineLauncherEarly.parent_path().parent_path().parent_path();

        if (request.m_buildGameplay)
        {
            if (engineBuildDir.empty())
            {
                wax::String msg{*m_alloc};
                msg.Append("Engine build dir not found for config '", 38);
                msg.Append(request.m_config.Data(), request.m_config.Size());
                msg.Append("'. Build it first: cmake --preset hive-game-", 44);
                wax::String lower{*m_alloc};
                for (size_t i = 0; i < request.m_config.Size(); ++i)
                {
                    lower.Append(
                        static_cast<char>(std::tolower(static_cast<unsigned char>(request.m_config.Data()[i]))));
                }
                msg.Append(lower.Data(), lower.Size());
                EmitStage(callback, userdata, ShipStage::FAILED, msg.CStr(), true);
                return false;
            }
            wax::String stageMsg{*m_alloc};
            stageMsg.Append("Building engine + gameplay (", 28);
            stageMsg.Append(request.m_config.Data(), request.m_config.Size());
            stageMsg.Append(")", 1);
            EmitStage(callback, userdata, ShipStage::BUILDING_GAMEPLAY, stageMsg.CStr());

            CMakeBuildRequest cmakeReq;
            const std::string buildDirGeneric = engineBuildDir.generic_string();
            cmakeReq.m_binaryDir = wax::StringView{buildDirGeneric.c_str(), buildDirGeneric.size()};
            cmakeReq.m_config = request.m_config;

            CallbackBundle bundle{callback, userdata};
            const int buildResult = RunCMakeBuild(cmakeReq, &CmakeLineCallback, &bundle);
            if (buildResult != 0)
            {
                EmitStage(callback, userdata, ShipStage::FAILED, "cmake --build failed", true);
                return false;
            }
        }

        EmitStage(callback, userdata, ShipStage::WALKING_SCENE, "Walking startup scene");
        const wax::StringView startupScene = m_project->Project().StartupSceneRelative();
        if (startupScene.IsEmpty())
        {
            EmitStage(callback, userdata, ShipStage::FAILED, "Project has no startup scene", true);
            return false;
        }

        scene::ReachableAssets reachable = scene::CollectFromScene(*m_registry, *m_project, startupScene, *m_alloc);

        if (request.m_forceFreshCook && reachable.m_cookedIds.Size() > 0)
        {
            EmitStage(callback, userdata, ShipStage::COOKING, "Cooking reachable assets");
            wax::Vector<nectar::AssetId> assetsCopy{*m_alloc};
            assetsCopy.Reserve(reachable.m_cookedIds.Size());
            for (size_t i = 0; i < reachable.m_cookedIds.Size(); ++i)
            {
                assetsCopy.PushBack(reachable.m_cookedIds[i]);
            }
            nectar::CookRequest cookReq;
            cookReq.m_assets = static_cast<wax::Vector<nectar::AssetId>&&>(assetsCopy);
            cookReq.m_platform = wax::StringView{"pc", 2};
            cookReq.m_workerCount = 1;
            (void)m_project->Cook().CookAll(cookReq);
            m_project->SaveImportCache();
        }

        EmitStage(callback, userdata, ShipStage::WRITING_PAK, "Writing assets.hivepak");
        nectar::ProjectFile strippedForPak{*m_alloc};
        nectar::ProjectDesc strippedDesc{};
        strippedDesc.m_name = m_project->Project().Name();
        strippedDesc.m_version = m_project->Project().Version();
        strippedDesc.m_startupScene = m_project->Project().StartupSceneRelative();
        strippedForPak.Create(strippedDesc);
        const wax::String strippedToml = strippedForPak.Serialize();

        const std::filesystem::path pakPath = outputDir / "assets.hivepak";
        const std::string pakPathStr = pakPath.generic_string();
        ShipPakInput pakIn;
        pakIn.m_assets = &reachable;
        pakIn.m_project = m_project;
        pakIn.m_outputPath = wax::StringView{pakPathStr.c_str(), pakPathStr.size()};
        pakIn.m_projectTomlBlob = wax::ByteSpan{
            reinterpret_cast<const uint8_t*>(strippedToml.CStr()), strippedToml.Size()};
        ShipPakStats pakStats{};
        CallbackBundle pakBundle{callback, userdata};
        if (!WriteShipPak(pakIn, &pakStats, &PakProgressCallback, &pakBundle, *m_alloc))
        {
            EmitStage(callback, userdata, ShipStage::FAILED, "WriteShipPak failed", true);
            return false;
        }

        EmitStage(callback, userdata, ShipStage::COPYING_RUNTIME, "Copying runtime files");
        const std::filesystem::path engineLauncher = FindEngineLauncher(request);
        if (engineLauncher.empty())
        {
            EmitStage(callback, userdata, ShipStage::FAILED,
                      "Engine retail launcher not found. Build it first: "
                      "cmake --build out/build/hive-game-retail --target hive_launcher",
                      true);
            return false;
        }
        if (!CopyFileSafe(engineLauncher, outputDir / "hive_launcher.exe", callback, userdata))
        {
            return false;
        }

        const std::filesystem::path gameplayDll =
            std::filesystem::path{projectRoot} / ".hive" / "modules" / "Retail" / "gameplay.dll";
        if (std::filesystem::exists(gameplayDll, ec))
        {
            (void)CopyFileSafe(gameplayDll, outputDir / "gameplay.dll", callback, userdata);
        }
        else
        {
            EmitStage(callback, userdata, ShipStage::COPYING_RUNTIME,
                      "Retail gameplay DLL not found; shipped game may fail to load gameplay", true);
        }

        const std::filesystem::path engineShaderSrc = engineLauncher.parent_path() / "shaders" / "engine";
        if (std::filesystem::is_directory(engineShaderSrc, ec))
        {
            const std::filesystem::path engineShaderDst = outputDir / "shaders" / "engine";
            std::filesystem::create_directories(engineShaderDst, ec);
            for (const auto& entry : std::filesystem::recursive_directory_iterator{engineShaderSrc, ec})
            {
                if (!entry.is_regular_file())
                {
                    continue;
                }
                const auto rel = std::filesystem::relative(entry.path(), engineShaderSrc, ec);
                if (ec)
                {
                    continue;
                }
                const std::filesystem::path dstFile = engineShaderDst / rel;
                (void)CopyFileSafe(entry.path(), dstFile, callback, userdata);
            }
        }

        const std::filesystem::path launcherDir = engineLauncher.parent_path();
        for (const auto& entry : std::filesystem::directory_iterator{launcherDir, ec})
        {
            if (!entry.is_regular_file())
            {
                continue;
            }
            auto ext = entry.path().extension().string();
            for (char& c : ext)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (ext != ".dll")
            {
                continue;
            }
            const auto fileName = entry.path().filename();
            (void)CopyFileSafe(entry.path(), outputDir / fileName, callback, userdata);
        }

        EmitStage(callback, userdata, ShipStage::WRITING_PROJECT_FILE, "Project metadata embedded in pak");
        EmitStage(callback, userdata, ShipStage::DONE, "Ship complete");
        hive::LogInfo(LOG_SHIP, "Ship complete: {} blobs, output {}", pakStats.m_blobCount, outputDirStr.c_str());
        return true;
    }
} // namespace waggle
