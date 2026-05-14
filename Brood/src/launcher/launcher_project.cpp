#include <hive/core/log.h>
#include <hive/hive_config.h>
#include <hive/platform/file_mapping.h>

#include <comb/default_allocator.h>

#include <wax/containers/string.h>

#include <nectar/database/asset_database.h>
#include <nectar/database/import_cache.h>
#include <nectar/material/material_importer.h>
#include <nectar/mesh/gltf_importer.h>
#include <nectar/pipeline/cook_pipeline.h>
#include <nectar/pipeline/import_pipeline.h>
#include <nectar/pipeline/passthrough_cookers.h>
#include <nectar/registry/hiveid_file.h>
#include <nectar/shader/shader_importer.h>
#include <nectar/shader_program/shader_program_importer.h>
#include <nectar/texture/texture_importer.h>

#include <waggle/app_context.h>
#include <waggle/project/project_context.h>
#include <waggle/project/project_scaffolder.h>

#include <swarm/swarm.h>

#include <terra/terra.h>

#include <waggle/render/render_module.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <launcher/launcher_platform.h>
#include <launcher/launcher_project.h>
#include <launcher/launcher_scene.h>

namespace brood::launcher
{

    static wax::String ResolveGameplayDllPath(const wax::String& projectRoot)
    {
#if HIVE_PLATFORM_WINDOWS
        constexpr const char* kDllName = "gameplay.dll";
#else
        constexpr const char* kDllName = "gameplay.so";
#endif

#if HIVE_CONFIG_DEBUG
        constexpr const char* kConfig = "Debug";
#elif HIVE_CONFIG_RELEASE
        constexpr const char* kConfig = "Release";
#elif HIVE_CONFIG_PROFILE
        constexpr const char* kConfig = "Profile";
#elif HIVE_CONFIG_RETAIL
        constexpr const char* kConfig = "Retail";
#else
        constexpr const char* kConfig = "Debug";
#endif

        std::error_code ec;

        auto configPath = projectRoot + "/.hive/modules/" + kConfig + "/" + kDllName;
        if (std::filesystem::exists(std::filesystem::path{configPath.CStr()}, ec) && !ec)
            return configPath;

        auto basePath = projectRoot + "/.hive/modules/" + kDllName;
        if (std::filesystem::exists(std::filesystem::path{basePath.CStr()}, ec) && !ec)
            return basePath;

        auto rootPath = projectRoot + "/" + kDllName;
        if (std::filesystem::exists(std::filesystem::path{rootPath.CStr()}, ec) && !ec)
            return rootPath;

        return {};
    }

    void TryLoadGameplayModule(waggle::EngineContext& ctx, LauncherState& state)
    {
        const wax::String root{state.m_project->Paths().m_root};

#if HIVE_MODE_EDITOR
        state.m_assetsRoot = state.m_project->Paths().m_assets;
#endif

        const wax::String dllPath = ResolveGameplayDllPath(root);
        if (dllPath.IsEmpty())
        {
            hive::LogInfo(LOG_LAUNCHER, "No gameplay DLL found for project at {}", root.CStr());
            return;
        }

        std::filesystem::path p{dllPath.CStr()};
        auto shadowFsPath = p.parent_path() / (p.stem().string() + "_live" + p.extension().string());
        std::error_code ec;
        std::filesystem::copy_file(p, shadowFsPath, std::filesystem::copy_options::overwrite_existing, ec);

        std::string shadowStr = shadowFsPath.string();
        const char* loadPath = ec ? dllPath.CStr() : shadowStr.c_str();

        hive::LogInfo(LOG_LAUNCHER, "TryLoadGameplayModule: attempting load of {}", loadPath);
        const bool loaded = state.m_gameplay.Load(loadPath);
        hive::LogInfo(LOG_LAUNCHER, "TryLoadGameplayModule: Load returned {}", loaded ? "true" : "false");
        if (loaded)
        {
            if (!state.m_gameplay.Register(*ctx.m_world))
                hive::LogWarning(LOG_LAUNCHER, "Gameplay DLL Register() failed");
        }
        else
        {
            hive::LogError(LOG_LAUNCHER, "Failed to load gameplay DLL. Auto-rebuild attempted={}",
                           state.m_gameplayAutoRebuildAttempted ? "true" : "false");
            if (!state.m_gameplayAutoRebuildAttempted)
            {
                state.m_gameplayAutoRebuildAttempted = true;
                state.m_gameplayBuildRequested = true;
                hive::LogError(LOG_LAUNCHER, "Triggering automatic gameplay module rebuild...");
            }
        }
    }

    namespace
    {
        void ReportProgress(BootProgressFn progress, void* userdata, const char* step, uint32_t current,
                            uint32_t total)
        {
            if (progress != nullptr)
            {
                progress(step, current, total, userdata);
            }
        }

        constexpr uint32_t kAssetsStampMagic = 0x53415348u; // "HSAS"
        constexpr uint32_t kAssetsStampEngineRev = 2u;

        struct AssetsStamp
        {
            uint32_t m_magic{};
            uint32_t m_engineRev{};
            uint64_t m_fileCount{};
            uint64_t m_totalSize{};
            int64_t m_maxMtimeNs{};
        };

        AssetsStamp ComputeAssetsStamp(const std::filesystem::path& assetsDir)
        {
            AssetsStamp stamp{};
            stamp.m_magic = kAssetsStampMagic;
            stamp.m_engineRev = kAssetsStampEngineRev;

            std::error_code ec;
            for (const auto& entry : std::filesystem::recursive_directory_iterator{assetsDir, ec})
            {
                if (ec || !entry.is_regular_file())
                {
                    continue;
                }
                std::error_code sizeEc;
                const auto size = entry.file_size(sizeEc);
                if (!sizeEc)
                {
                    stamp.m_totalSize += static_cast<uint64_t>(size);
                }

                std::error_code timeEc;
                const auto mtime = entry.last_write_time(timeEc);
                if (!timeEc)
                {
                    const auto ns =
                        std::chrono::duration_cast<std::chrono::nanoseconds>(mtime.time_since_epoch()).count();
                    if (ns > stamp.m_maxMtimeNs)
                    {
                        stamp.m_maxMtimeNs = ns;
                    }
                }
                ++stamp.m_fileCount;
            }
            return stamp;
        }

        bool ReadAssetsStamp(const std::filesystem::path& path, AssetsStamp& out)
        {
            hive::FileMapping mapping{};
            if (!mapping.Open(path.string().c_str()) || mapping.Size() != sizeof(AssetsStamp))
            {
                return false;
            }
            std::memcpy(&out, mapping.Data(), sizeof(AssetsStamp));
            return out.m_magic == kAssetsStampMagic;
        }

        // No hive::FileMapping equivalent for writes yet — std::fopen is the engine fallback for
        // small launcher-local artifacts. Brood lives in the std-tolerant zone (see CODE_REVIEW.md).
        bool WriteAssetsStamp(const std::filesystem::path& path, const AssetsStamp& stamp)
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            FILE* file = nullptr;
#ifdef _MSC_VER
            fopen_s(&file, path.string().c_str(), "wb");
#else
            file = std::fopen(path.string().c_str(), "wb");
#endif
            if (file == nullptr)
            {
                return false;
            }
            const size_t written = std::fwrite(&stamp, 1, sizeof(stamp), file);
            std::fclose(file);
            return written == sizeof(stamp);
        }

        bool AssetsStampsMatch(const AssetsStamp& lhs, const AssetsStamp& rhs)
        {
            return lhs.m_magic == rhs.m_magic && lhs.m_engineRev == rhs.m_engineRev &&
                   lhs.m_fileCount == rhs.m_fileCount && lhs.m_totalSize == rhs.m_totalSize &&
                   lhs.m_maxMtimeNs == rhs.m_maxMtimeNs;
        }
    } // namespace

    bool OpenProject(waggle::EngineContext& ctx, LauncherState& state, const std::filesystem::path& requestedPath,
                     BootProgressFn progress, void* progressUd)
    {
        if (state.m_project == nullptr)
        {
            return false;
        }

        if (state.m_projectOpen)
        {
#if HIVE_MODE_EDITOR
            SetHubStatus(state.m_hub, true, "A project is already open in this launcher session.");
#endif
            return false;
        }

        std::filesystem::path resolvedPath{};
        if (!ResolveProjectFilePath(requestedPath, &resolvedPath))
        {
#if HIVE_MODE_EDITOR
            SetHubStatus(state.m_hub, true, "The selected path does not contain a valid project.hive file.");
#endif
            return false;
        }

        const wax::String projectPath{resolvedPath.generic_string().c_str()};
        const waggle::ProjectConfig projectConfig{
            .m_enableHotReload = HIVE_FEATURE_HOT_RELOAD != 0,
            .m_watcherIntervalMs = 500,
        };

        if (!state.m_project->Open({projectPath.CStr(), projectPath.Size()}, projectConfig))
        {
#if HIVE_MODE_EDITOR
            SetHubStatus(state.m_hub, true, "Failed to open the selected project.");
#endif
            hive::LogError(LOG_LAUNCHER, "Failed to open project: {}", projectPath.CStr());
            return false;
        }

        state.m_projectPath = projectPath;
        state.m_projectOpen = true;

        static nectar::GltfImporter s_gltfImporter;
        static nectar::TextureImporter s_textureImporter;
        static nectar::MaterialImporter s_materialImporter;
        static nectar::ShaderImporter s_shaderImporter;
        static nectar::ShaderProgramImporter s_shaderProgramImporter;
        static nectar::PassthroughMeshCooker s_meshCooker;
        static nectar::PassthroughTextureCooker s_textureCooker;
        static nectar::PassthroughMaterialCooker s_materialCooker;
        static nectar::PassthroughShaderCooker s_shaderCooker;
        static nectar::PassthroughShaderProgramCooker s_shaderProgramCooker;
        state.m_project->RegisterImporter(&s_gltfImporter);
        state.m_project->RegisterImporter(&s_textureImporter);
        state.m_project->RegisterImporter(&s_materialImporter);
        state.m_project->RegisterImporter(&s_shaderImporter);
        state.m_project->RegisterImporter(&s_shaderProgramImporter);
        state.m_project->RegisterCooker(&s_meshCooker);
        state.m_project->RegisterCooker(&s_textureCooker);
        state.m_project->RegisterCooker(&s_materialCooker);
        state.m_project->RegisterCooker(&s_shaderCooker);
        state.m_project->RegisterCooker(&s_shaderProgramCooker);

        const auto& project = state.m_project->Project();
        hive::LogInfo(LOG_LAUNCHER, "Project '{}' v{}", std::string_view{project.Name().Data(), project.Name().Size()},
                      std::string_view{project.Version().Data(), project.Version().Size()});

        ctx.m_world->InsertResource(waggle::ProjectContext{state.m_project});

        if (ctx.m_window != nullptr)
        {
            const wax::String windowTitle = BuildWindowTitle(projectPath);
            terra::SetWindowTitle(ctx.m_window, windowTitle.CStr());
        }

        const std::filesystem::path projectAssetsDir =
            std::filesystem::path{projectPath.CStr()}.parent_path() / "assets";
        const std::filesystem::path projectShaderDir = projectAssetsDir / "shaders";
        if (ctx.m_renderContext != nullptr)
        {
            swarm::AddShaderSearchPath(ctx.m_renderContext, projectShaderDir.string().c_str());
        }

        {
            const std::filesystem::path engineShaderDir =
                GetCurrentExecutablePath().parent_path() / "shaders" / "engine";
            if (std::filesystem::is_directory(engineShaderDir))
            {
                const auto engineShaderStr = engineShaderDir.generic_string();
                state.m_project->MountEngineAssets(wax::StringView{engineShaderStr.c_str(), engineShaderStr.size()},
                                                   wax::StringView{"engine"});
                hive::LogInfo(LOG_LAUNCHER, "Mounted engine assets at 'engine/' -> {}", engineShaderStr);
            }
        }

        if (std::filesystem::is_directory(projectAssetsDir))
        {
            const auto scanStart = std::chrono::steady_clock::now();
            const std::filesystem::path stampPath =
                std::filesystem::path{projectPath.CStr()}.parent_path() / ".hive" / "import_stamp.bin";
            const AssetsStamp currentStamp = ComputeAssetsStamp(projectAssetsDir);
            AssetsStamp cachedStamp{};
            const bool stampHit = ReadAssetsStamp(stampPath, cachedStamp) &&
                                  AssetsStampsMatch(currentStamp, cachedStamp);

            if (stampHit)
            {
                hive::LogInfo(LOG_LAUNCHER, "Assets stamp hit (files={}, size={} bytes); skipping scan",
                              currentStamp.m_fileCount, currentStamp.m_totalSize);
                ReportProgress(progress, progressUd, "Scanning assets",
                               static_cast<uint32_t>(currentStamp.m_fileCount),
                               static_cast<uint32_t>(currentStamp.m_fileCount));
            }
            else
            {
                struct Candidate
                {
                    std::string m_relPath;
                    std::string m_absPath;
                    bool m_isShader{};
                    bool m_isTexture{};
                };

                std::vector<Candidate> candidates;
                candidates.reserve(256);
                {
                    std::error_code ec;
                    for (const auto& entry : std::filesystem::recursive_directory_iterator{projectAssetsDir, ec})
                    {
                        if (ec || !entry.is_regular_file())
                        {
                            continue;
                        }
                        auto ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(),
                                       [](unsigned char c) { return std::tolower(c); });
                        const bool isShader = (ext == ".hlsl");
                        const bool isShaderProgram = (ext == ".hshader");
                        const bool isMaterial = (ext == ".hmat");
                        const bool isTexture = (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".tga" ||
                                                ext == ".bmp" || ext == ".hdr");
                        if (!isShader && !isShaderProgram && !isMaterial && !isTexture)
                        {
                            continue;
                        }
                        auto relPath =
                            std::filesystem::relative(entry.path(), projectAssetsDir, ec).generic_string();
                        if (ec || relPath.empty())
                        {
                            continue;
                        }
                        Candidate candidate;
                        candidate.m_relPath = std::move(relPath);
                        candidate.m_absPath = entry.path().string();
                        candidate.m_isShader = isShader;
                        candidate.m_isTexture = isTexture;
                        candidates.push_back(std::move(candidate));
                    }
                }

                const uint32_t scanTotal = static_cast<uint32_t>(candidates.size());
                ReportProgress(progress, progressUd, "Scanning assets", 0, scanTotal);

                wax::Vector<nectar::AssetId> shaderAssets{comb::GetDefaultAllocator()};
                wax::Vector<nectar::AssetId> otherAssets{comb::GetDefaultAllocator()};
                for (uint32_t i = 0; i < candidates.size(); ++i)
                {
                    const auto& candidate = candidates[i];
                    // Throttle progress callbacks: each one invokes processEvents which is
                    // expensive at scan-frequency on large asset trees.
                    if ((i & 0xF) == 0)
                    {
                        ReportProgress(progress, progressUd, "Scanning assets", i, scanTotal);
                    }

                    nectar::AssetId id = nectar::AssetIdFromPath(candidate.m_relPath.c_str());
                    if (candidate.m_isTexture)
                    {
                        nectar::HiveIdData hid{};
                        const auto hiveidPath = candidate.m_absPath + ".hiveid";
                        if (nectar::ReadHiveId(hiveidPath.c_str(), hid, comb::GetDefaultAllocator()) &&
                            hid.m_guid.IsValid())
                        {
                            id = hid.m_guid;
                        }
                    }
                    const bool needsImport = !state.m_project->Database().Contains(id) ||
                                             state.m_project->Import().NeedsReimport(id);
                    if (needsImport)
                    {
                        nectar::ImportRequest req;
                        req.m_sourcePath = wax::StringView{candidate.m_relPath.c_str(), candidate.m_relPath.size()};
                        req.m_assetId = id;
                        const auto output = state.m_project->Import().ImportAsset(req);
                        if (!output.m_success)
                        {
                            hive::LogWarning(LOG_LAUNCHER, "Auto-import failed for '{}': {}",
                                             candidate.m_relPath, output.m_errorMessage.CStr());
                            continue;
                        }
                    }
                    if (candidate.m_isShader)
                    {
                        shaderAssets.PushBack(id);
                    }
                    else
                    {
                        otherAssets.PushBack(id);
                    }
                }
                ReportProgress(progress, progressUd, "Scanning assets", scanTotal, scanTotal);

                const size_t totalImported = shaderAssets.Size() + otherAssets.Size();
                if (totalImported > 0)
                {
                    hive::LogInfo(LOG_LAUNCHER, "Auto-imported {} project asset(s)", totalImported);
                    wax::Vector<nectar::AssetId> all{comb::GetDefaultAllocator()};
                    for (size_t i = 0; i < shaderAssets.Size(); ++i)
                    {
                        all.PushBack(shaderAssets[i]);
                    }
                    for (size_t i = 0; i < otherAssets.Size(); ++i)
                    {
                        all.PushBack(otherAssets[i]);
                    }
                    const uint32_t cookTotal = static_cast<uint32_t>(all.Size());
                    ReportProgress(progress, progressUd, "Cooking assets", 0, cookTotal);
                    nectar::CookRequest req;
                    req.m_assets = static_cast<wax::Vector<nectar::AssetId>&&>(all);
                    req.m_platform = wax::StringView{"pc"};
                    req.m_workerCount = 1;
                    (void)state.m_project->Cook().CookAll(req);
                    ReportProgress(progress, progressUd, "Cooking assets", cookTotal, cookTotal);
                    state.m_project->SaveImportCache();
                }

                (void)WriteAssetsStamp(stampPath, currentStamp);
            }
            const auto scanEnd = std::chrono::steady_clock::now();
            const double scanMs = std::chrono::duration<double, std::milli>(scanEnd - scanStart).count();
            hive::LogInfo(LOG_LAUNCHER, "Asset scan + import + cook completed in {:.1f} ms (stamp {})", scanMs,
                          stampHit ? "hit" : "miss");
        }

#if HIVE_MODE_EDITOR
        ResetSceneEditorState(state);
        state.m_currentScenePath.Clear();
        state.m_currentSceneRelative.Clear();

        const wax::StringView startupScene = state.m_project->Project().StartupSceneRelative();
        if (!startupScene.IsEmpty())
        {
            ReportProgress(progress, progressUd, "Loading scene", 0, 1);
            const std::filesystem::path startupScenePath = ResolveScenePath(*state.m_project, startupScene);
            if (!LoadEditorScene(ctx, state, startupScenePath))
            {
                hive::LogWarning(LOG_LAUNCHER, "Failed to load startup scene: {}", startupScenePath.generic_string());
            }
            ReportProgress(progress, progressUd, "Loading scene", 1, 1);

            if (ctx.m_renderModule != nullptr)
            {
                const auto preloadStart = std::chrono::steady_clock::now();
                ctx.m_renderModule->PreloadScene(*ctx.m_world, state.m_project, progress, progressUd);
                const auto preloadEnd = std::chrono::steady_clock::now();
                const double preloadMs =
                    std::chrono::duration<double, std::milli>(preloadEnd - preloadStart).count();
                hive::LogInfo(LOG_LAUNCHER, "Scene preload completed in {:.1f} ms", preloadMs);
            }
        }
        QueueSceneRecoveryPrompt(state);
#endif
        ReportProgress(progress, progressUd, "Loading gameplay module", 0, 1);
        TryLoadGameplayModule(ctx, state);
        ReportProgress(progress, progressUd, "Loading gameplay module", 1, 1);

#if HIVE_MODE_EDITOR
        SetHubStatus(state.m_hub, false, "Project ready.");
#endif

        if (state.m_exitAfterSetup)
        {
            ctx.m_app->RequestStop();
        }

        return true;
    }

#if HIVE_MODE_EDITOR
    bool CreateProjectFromHub(waggle::EngineContext& ctx, LauncherState& state, BootProgressFn progress,
                              void* progressUd)
    {
        ProjectHubState& hub = state.m_hub;
        const wax::String projectName = TrimmedCopy(hub.m_createName.Data());
        if (!IsProjectNameValid(projectName))
        {
            SetHubStatus(hub, true, "Project name must use only letters, numbers, '_' or '-'.");
            return false;
        }

        const wax::String version = TrimmedCopy(hub.m_createVersion.Data()).IsEmpty()
                                        ? wax::String{"0.1.0"}
                                        : TrimmedCopy(hub.m_createVersion.Data());
        if (hub.m_engineRoot.empty())
        {
            SetHubStatus(hub, true,
                         "Unable to locate the HiveEngine root. Set HIVE_ENGINE_DIR or run from an engine build.");
            return false;
        }

        if (!hub.m_supportEditor && !hub.m_supportGame && !hub.m_supportHeadless)
        {
            SetHubStatus(hub, true, "Enable at least one runtime mode before creating the project.");
            return false;
        }

        const std::filesystem::path targetRoot = BuildCreateTargetPath(hub, projectName);
        if (targetRoot.empty())
        {
            SetHubStatus(hub, true, "Choose a destination directory before creating the project.");
            return false;
        }

        std::error_code ec;
        if (std::filesystem::exists(targetRoot, ec) && !ec)
        {
            if (!std::filesystem::is_directory(targetRoot, ec))
            {
                SetHubStatus(hub, true, "The destination already exists and is not a directory.");
                return false;
            }

            std::filesystem::directory_iterator end{};
            std::filesystem::directory_iterator it{targetRoot, ec};
            if (!ec && it != end)
            {
                SetHubStatus(hub, true, "The destination directory is not empty.");
                return false;
            }
        }

        const bool wantsGraphics = hub.m_supportEditor || hub.m_supportGame;
        const wax::String engineRoot{hub.m_engineRoot.generic_string().c_str()};
        const wax::String targetRootString{targetRoot.generic_string().c_str()};
        const wax::String presetBase{GetPresetBase(hub.m_toolchain)};
        const wax::String runtimeBackend{wantsGraphics ? "vulkan" : ""};

        waggle::ProjectScaffoldConfig scaffoldConfig{};
        scaffoldConfig.m_cmake.m_projectName = {projectName.CStr(), projectName.Size()};
        scaffoldConfig.m_cmake.m_projectRoot = {targetRootString.CStr(), targetRootString.Size()};
        scaffoldConfig.m_cmake.m_enginePath = {engineRoot.CStr(), engineRoot.Size()};
        scaffoldConfig.m_cmake.m_linkSwarm = wantsGraphics;
        scaffoldConfig.m_cmake.m_linkTerra = wantsGraphics;
        scaffoldConfig.m_cmake.m_linkAntennae = wantsGraphics;
        scaffoldConfig.m_projectVersion = {version.CStr(), version.Size()};
        scaffoldConfig.m_runtimeBackend = {runtimeBackend.CStr(), runtimeBackend.Size()};
        scaffoldConfig.m_presetBase = {presetBase.CStr(), presetBase.Size()};
        scaffoldConfig.m_supportEditor = hub.m_supportEditor;
        scaffoldConfig.m_supportGame = hub.m_supportGame;
        scaffoldConfig.m_supportHeadless = hub.m_supportHeadless;

        if (!waggle::ProjectScaffolder::WriteToProject(scaffoldConfig, state.m_alloc.Get()))
        {
            SetHubStatus(hub, true, "Project files could not be generated.");
            return false;
        }

        RefreshProjectHub(hub);
        CopyStringToBuffer(wax::String{(targetRoot / "project.hive").generic_string().c_str()}, hub.m_openPath.Data(),
                           hub.m_openPath.Size());

        return OpenProject(ctx, state, targetRoot / "project.hive", progress, progressUd);
    }
#endif

} // namespace brood::launcher
