#include <hive/core/log.h>

#include <queen/reflect/component_registry.h>
#include <queen/world/world.h>

#include <nectar/mesh/mesh_data.h>

#include <waggle/components/mesh_reference.h>
#include <waggle/components/name.h>
#include <waggle/components/transform.h>

#include <forge/asset_browser.h>
#include <forge/blueprint_editor.h>
#include <forge/blueprint_node_inspector.h>
#include <forge/blueprint_outline_panel.h>
#include <forge/blueprint_panel.h>
#include <forge/console_panel.h>
#include <forge/editor_undo.h>
#include <forge/forge_main_window.h>
#include <forge/gizmo_interaction.h>
#include <forge/hierarchy_panel.h>
#include <forge/inspector_panel.h>
#include <forge/progress_overlay.h>
#include <forge/project_hub.h>
#include <forge/selection.h>
#include <forge/toolbar.h>
#include <forge/vulkan_viewport_widget.h>

#include <QCloseEvent>
#include <QDockWidget>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <propolis/runtime/function_registry.h>
#include <vector>

static const hive::LogCategory LOG_FORGE{"Forge"};

namespace
{
    std::string ResolveMeshVfsPath(const std::filesystem::path& meshPath, const std::filesystem::path& assetsRoot,
                                   const std::string& fallbackName)
    {
        if (assetsRoot.empty())
        {
            return fallbackName;
        }
        std::error_code ec;
        auto relative = std::filesystem::relative(meshPath, assetsRoot, ec);
        if (ec || relative.empty())
        {
            return fallbackName;
        }
        return relative.generic_string();
    }

    bool ReadNmshSubmeshes(const std::filesystem::path& meshPath, std::vector<nectar::SubMesh>* submeshes)
    {
        FILE* file{nullptr};
#ifdef _WIN32
        fopen_s(&file, meshPath.string().c_str(), "rb");
#else
        file = fopen(meshPath.string().c_str(), "rb");
#endif
        if (file == nullptr)
        {
            return false;
        }

        nectar::NmshHeader header{};
        bool ok = false;
        if (std::fread(&header, sizeof(header), 1, file) == 1 && header.m_magic == nectar::kNmshMagic)
        {
            submeshes->resize(header.m_submeshCount);
            std::fread(submeshes->data(), sizeof(nectar::SubMesh), header.m_submeshCount, file);
            ok = true;
        }
        std::fclose(file);
        return ok;
    }

    waggle::MeshReference MakeMeshReference(const std::string& meshVfsPath, int32_t submeshIndex,
                                            const nectar::SubMesh* submesh)
    {
        waggle::MeshReference ref{};
        ref.m_meshName = wax::FixedString{meshVfsPath.c_str()};
        if (submesh != nullptr)
        {
            ref.m_meshIndex = submeshIndex;
            ref.m_indexCount = submesh->m_indexCount;
            ref.m_materialIndex = submesh->m_materialIndex;
            const std::filesystem::path meshFsPath{meshVfsPath};
            const auto modelDir = meshFsPath.parent_path().generic_string();
            if (!modelDir.empty() && submesh->m_materialIndex >= 0)
            {
                char hmatPath[wax::FixedString::MaxCapacity + 1];
                std::snprintf(hmatPath, sizeof(hmatPath), "%s/Material_%d.hmat", modelDir.c_str(),
                              submesh->m_materialIndex);
                ref.m_material = wax::FixedString{hmatPath};
            }
        }
        else
        {
            ref.m_indexCount = 1;
        }
        return ref;
    }

    void SpawnNmshEntities(queen::World& world, const std::filesystem::path& meshPath,
                           const std::filesystem::path& assetsRoot)
    {
        const auto meshName = meshPath.stem().string();
        const auto meshVfsPath = ResolveMeshVfsPath(meshPath, assetsRoot, meshName);

        std::vector<nectar::SubMesh> submeshes;
        ReadNmshSubmeshes(meshPath, &submeshes);

        if (submeshes.size() <= 1)
        {
            auto ref = MakeMeshReference(meshVfsPath, 0, nullptr);
            (void)world.Spawn(waggle::Name{wax::FixedString{meshName.c_str()}}, waggle::Transform{},
                              waggle::WorldMatrix{}, waggle::TransformVersion{}, std::move(ref));
            hive::LogInfo(LOG_FORGE, "Spawned mesh entity: {} ({})", meshName, meshVfsPath);
            return;
        }

        queen::Entity root = world.Spawn(waggle::Name{wax::FixedString{meshName.c_str()}}, waggle::Transform{},
                                         waggle::WorldMatrix{}, waggle::TransformVersion{});

        for (uint32_t i = 0; i < submeshes.size(); ++i)
        {
            char primName[128];
            std::snprintf(primName, sizeof(primName), "%s_prim%u", meshName.c_str(), i);
            auto ref = MakeMeshReference(meshVfsPath, static_cast<int32_t>(i), &submeshes[i]);
            queen::Entity child = world.Spawn(waggle::Name{wax::FixedString{primName}}, waggle::Transform{},
                                              waggle::WorldMatrix{}, waggle::TransformVersion{}, std::move(ref));
            world.SetParent(child, root);
        }
        hive::LogInfo(LOG_FORGE, "Spawned mesh hierarchy: {} ({} primitives)", meshName, submeshes.size());
    }
} // namespace

static QIcon MakeHexIcon()
{
    QPixmap pix{64, 64};
    pix.fill(Qt::transparent);

    QPainter p{&pix};
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(32.0, 32.0);

    auto drawHex = [&](double r, QColor color) {
        QPainterPath hex;
        for (int i = 0; i < 6; ++i)
        {
            double a = (60.0 * i - 30.0) * 3.14159265 / 180.0;
            QPointF pt{r * std::cos(a), r * std::sin(a)};
            if (i == 0)
                hex.moveTo(pt);
            else
                hex.lineTo(pt);
        }
        hex.closeSubpath();
        p.setPen(QPen{color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin});
        p.setBrush(Qt::NoBrush);
        p.drawPath(hex);
    };

    drawHex(26.0, QColor{0xf0, 0xa5, 0x00, 180});
    drawHex(18.0, QColor{0xf0, 0xa5, 0x00});
    drawHex(10.0, QColor{0xf0, 0xa5, 0x00, 90});
    p.end();

    return QIcon{pix};
}

namespace forge
{
    ForgeMainWindow::ForgeMainWindow(QWidget* parent)
        : QMainWindow{parent}
    {
        setWindowTitle("HiveEngine");
        setWindowIcon(MakeHexIcon());
        resize(1200, 750);

        m_hub = new ProjectHub{this};
        setCentralWidget(m_hub);

        connect(m_hub, &ProjectHub::projectSelected, this, &ForgeMainWindow::hubProjectSelected);
        connect(m_hub, &ProjectHub::createProjectRequested, this, &ForgeMainWindow::hubCreateRequested);
        connect(m_hub, &ProjectHub::browseProjectRequested, this, &ForgeMainWindow::hubBrowseRequested);
    }

    void ForgeMainWindow::Initialize(queen::World* world, EditorSelection* selection,
                                     const queen::ComponentRegistry<256>* registry)
    {
        m_world = world;
        m_selection = selection;
        m_registry = registry;
    }

    void ForgeMainWindow::ShowHub(const std::vector<DiscoveredProject>& projects)
    {
        menuBar()->hide();
        for (auto* dock : m_docks)
            dock->hide();

        m_hub->SetProjects(projects);
        m_hub->show();
        setCentralWidget(m_hub);
        setWindowTitle("HiveEngine");
        resize(1200, 750);
    }

    class HexSpinner : public QWidget
    {
    public:
        explicit HexSpinner(QWidget* parent = nullptr)
            : QWidget{parent}
        {
            setFixedSize(48, 48);
            m_timer.setInterval(16);
            connect(&m_timer, &QTimer::timeout, this, [this] {
                m_angle += 3.0;
                update();
            });
            m_timer.start();
        }

    protected:
        void paintEvent(QPaintEvent*) override
        {
            QPainter p{this};
            p.setRenderHint(QPainter::Antialiasing);
            p.translate(width() / 2.0, height() / 2.0);
            p.rotate(m_angle);

            const double r = 18.0;
            QPainterPath hex;
            for (int i = 0; i < 6; ++i)
            {
                double a = (60.0 * i - 30.0) * 3.14159265 / 180.0;
                QPointF pt{r * std::cos(a), r * std::sin(a)};
                if (i == 0)
                    hex.moveTo(pt);
                else
                    hex.lineTo(pt);
            }
            hex.closeSubpath();

            QPen pen{QColor{0xf0, 0xa5, 0x00}, 2.5};
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(hex);

            p.setPen(QPen{QColor{0xf0, 0xa5, 0x00, 60}, 2.0});
            p.rotate(30.0);
            p.drawPath(hex);
        }

    private:
        QTimer m_timer;
        double m_angle{0.0};
    };

    void ForgeMainWindow::ShowLoading(const QString& projectName)
    {
        m_hub->hide();
        menuBar()->hide();

        m_loadingWidget = new QWidget{this};
        m_loadingWidget->setStyleSheet("background-color: #0d0d0d;");

        auto* layout = new QVBoxLayout{m_loadingWidget};
        layout->setAlignment(Qt::AlignCenter);
        layout->setSpacing(0);

        auto* spinnerRow = new QHBoxLayout;
        spinnerRow->setAlignment(Qt::AlignCenter);
        spinnerRow->addWidget(new HexSpinner{m_loadingWidget});
        layout->addLayout(spinnerRow);

        layout->addSpacing(24);

        auto* title = new QLabel{"Hive Engine"};
        title->setAlignment(Qt::AlignCenter);
        title->setStyleSheet("color: #f0a500; font-size: 24px; font-weight: bold; letter-spacing: 4px;");
        layout->addWidget(title);

        layout->addSpacing(8);

        auto* sub = new QLabel{QString{"Loading %1"}.arg(projectName)};
        sub->setAlignment(Qt::AlignCenter);
        sub->setStyleSheet("color: #555; font-size: 12px;");
        layout->addWidget(sub);

        layout->addSpacing(20);

        auto* bar = new QWidget{m_loadingWidget};
        bar->setFixedSize(200, 2);
        bar->setStyleSheet("background-color: #1a1a1a; border-radius: 1px;");

        auto* barGlow = new QWidget{bar};
        barGlow->setFixedSize(60, 2);
        barGlow->setStyleSheet("background-color: #f0a500; border-radius: 1px;");

        auto* barAnim = new QPropertyAnimation{barGlow, "pos", m_loadingWidget};
        barAnim->setDuration(1200);
        barAnim->setStartValue(QPoint{0, 0});
        barAnim->setEndValue(QPoint{140, 0});
        barAnim->setEasingCurve(QEasingCurve::InOutSine);
        barAnim->setLoopCount(-1);
        barAnim->start();

        auto* barRow = new QHBoxLayout;
        barRow->setAlignment(Qt::AlignCenter);
        barRow->addWidget(bar);
        layout->addLayout(barRow);

        setCentralWidget(m_loadingWidget);
        setWindowTitle(QString{"HiveEngine - %1"}.arg(projectName));
    }

    void ForgeMainWindow::ShowEditor()
    {
        if (m_loadingWidget != nullptr)
        {
            m_loadingWidget->hide();
            m_loadingWidget = nullptr;
        }
        m_hub->hide();

        m_editorUndo = new EditorUndoManager{};
        m_gizmoBridge = new GizmoInteractionBridge{};
        if (m_world != nullptr && m_selection != nullptr)
        {
            m_gizmoBridge->SetContext(m_world, m_selection, m_editorUndo);
        }

        CreateMenus();

        m_toolbar = new EditorToolbar{this};
        m_toolbar->setMovable(false);
        addToolBar(Qt::TopToolBarArea, m_toolbar);

        CreateDocks();
        ConnectSignals();
        menuBar()->show();

        setWindowTitle("HiveEngine");
        resize(1600, 900);
    }

    void ForgeMainWindow::closeEvent(QCloseEvent* event)
    {
        if (!PromptAllDirtyBlueprints())
        {
            event->ignore();
            return;
        }

        if (m_sceneDirty)
        {
            auto answer = QMessageBox::question(
                this, "Unsaved Changes", "The current scene has unsaved changes. Do you want to save before closing?",
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

            if (answer == QMessageBox::Cancel)
            {
                event->ignore();
                return;
            }
            if (answer == QMessageBox::Save)
            {
                emit saveRequested();
            }
        }
        emit editorCloseRequested();
        event->accept();
    }

    void ForgeMainWindow::RefreshAll()
    {
        if (m_world && m_hierarchy)
            m_hierarchy->Refresh(*m_world);

        if (m_world && m_inspector && m_selection && m_registry)
            m_inspector->Refresh(*m_world, *m_selection, *m_registry, *m_editorUndo);

        if (m_assetBrowser)
            m_assetBrowser->Refresh();
    }

    void ForgeMainWindow::SetSceneDirty(bool dirty)
    {
        m_sceneDirty = dirty;
        QString title = "HiveEngine";
        if (!m_sceneName.isEmpty())
        {
            title = QString{"HiveEngine - %1"}.arg(m_sceneName);
        }
        if (m_sceneDirty)
        {
            title += " *";
        }
        setWindowTitle(title);
    }

    void ForgeMainWindow::SetSceneName(const QString& name)
    {
        m_sceneName = name;
        SetSceneDirty(m_sceneDirty);
    }

    void ForgeMainWindow::SetAssetsRoot(const char* path)
    {
        if (m_assetBrowser)
            m_assetBrowser->SetAssetsRoot(path);
    }

    EditorToolbar* ForgeMainWindow::Toolbar()
    {
        return m_toolbar;
    }

    void ForgeMainWindow::AttachViewport(terra::WindowContext* window, swarm::RenderContext* renderContext)
    {
        if (m_viewport)
        {
            m_viewport->EmbedGlfwWindow(window);
            m_viewport->SetRenderContext(renderContext);
        }
    }

    void ForgeMainWindow::PollEditorInput()
    {
        if (m_viewport == nullptr)
        {
            return;
        }
        auto* bridge = m_viewport->InputBridge();
        if (bridge != nullptr)
        {
            bridge->PollKeyboard();
        }
    }

    void ForgeMainWindow::TickGizmoInteraction()
    {
        if (m_gizmoBridge == nullptr || m_viewport == nullptr)
        {
            return;
        }
        const float viewportWidth = static_cast<float>(m_viewport->width());
        const float viewportHeight = static_cast<float>(m_viewport->height());
        m_gizmoBridge->Tick(viewportWidth, viewportHeight);
    }

    void ForgeMainWindow::CreateMenus()
    {
        menuBar()->clear();

        auto* fileMenu = menuBar()->addMenu("&File");

        auto* openAction = fileMenu->addAction("&Open Scene");
        openAction->setShortcut(QKeySequence{"Ctrl+O"});
        connect(openAction, &QAction::triggered, this, &ForgeMainWindow::openRequested);

        fileMenu->addSeparator();

        auto* saveAction = fileMenu->addAction("&Save");
        saveAction->setShortcut(QKeySequence{"Ctrl+S"});
        connect(saveAction, &QAction::triggered, this, [this]() {
            auto* panel = qobject_cast<BlueprintPanel*>(m_centralTabs->currentWidget());
            if (panel)
            {
                panel->Save();
            }
            else
            {
                emit saveRequested();
            }
        });

        auto* saveAsAction = fileMenu->addAction("Save Scene &As...");
        saveAsAction->setShortcut(QKeySequence{"Ctrl+Shift+S"});
        connect(saveAsAction, &QAction::triggered, this, &ForgeMainWindow::saveAsRequested);

        fileMenu->addSeparator();

        auto* quitAction = fileMenu->addAction("&Quit");
        quitAction->setShortcut(QKeySequence{"Alt+F4"});
        connect(quitAction, &QAction::triggered, this, &QMainWindow::close);

        auto* editMenu = menuBar()->addMenu("&Edit");

        auto* undoAction = editMenu->addAction("&Undo");
        undoAction->setShortcut(QKeySequence{"Ctrl+Z"});
        connect(undoAction, &QAction::triggered, this, [this] {
            if (m_editorUndo && m_editorUndo->Undo())
                RefreshAll();
        });

        auto* redoAction = editMenu->addAction("&Redo");
        redoAction->setShortcuts({QKeySequence{"Ctrl+Y"}, QKeySequence{"Ctrl+Shift+Z"}});
        connect(redoAction, &QAction::triggered, this, [this] {
            if (m_editorUndo && m_editorUndo->Redo())
                RefreshAll();
        });
    }

    void ForgeMainWindow::CreateDocks()
    {
        // Left dock: stacked Hierarchy / Blueprint Outline
        m_hierarchy = new HierarchyPanel{*m_selection, *m_editorUndo, this};
        m_blueprintOutline = new BlueprintOutlinePanel{this};

        m_leftStack = new QStackedWidget{this};
        m_leftStack->addWidget(m_hierarchy);
        m_leftStack->addWidget(m_blueprintOutline);
        m_leftStack->setCurrentIndex(0);

        m_leftDock = new QDockWidget{"Hierarchy", this};
        m_leftDock->setWidget(m_leftStack);
        addDockWidget(Qt::LeftDockWidgetArea, m_leftDock);
        m_docks.append(m_leftDock);

        // Right dock: stacked Inspector / Blueprint Node Inspector
        m_inspector = new InspectorPanel{this};
        m_blueprintInspector = new BlueprintNodeInspector{this};
        m_blueprintInspector->SetComponentRegistry(m_registry);

        m_rightStack = new QStackedWidget{this};
        m_rightStack->addWidget(m_inspector);
        m_rightStack->addWidget(m_blueprintInspector);
        m_rightStack->setCurrentIndex(0);

        auto* inspectorDock = new QDockWidget{"Inspector", this};
        inspectorDock->setWidget(m_rightStack);
        inspectorDock->setMinimumWidth(300);
        addDockWidget(Qt::RightDockWidgetArea, inspectorDock);
        m_docks.append(inspectorDock);

        auto* lockBtn = new QToolButton{inspectorDock};
        lockBtn->setText("\xf0\x9f\x94\x93");
        lockBtn->setCheckable(true);
        lockBtn->setChecked(false);
        lockBtn->setCursor(Qt::PointingHandCursor);
        lockBtn->setToolTip("Lock Inspector");
        lockBtn->setStyleSheet(
            "QToolButton { background: transparent; border: none; font-size: 14px; padding: 2px 6px; }"
            "QToolButton:checked { color: #f0a500; }");
        connect(lockBtn, &QToolButton::toggled, this, [this, lockBtn](bool checked) {
            m_inspectorLocked = checked;
            lockBtn->setText(checked ? "\xf0\x9f\x94\x92" : "\xf0\x9f\x94\x93");
        });
        inspectorDock->setTitleBarWidget(nullptr);

        auto* titleBar = new QWidget{inspectorDock};
        auto* titleLayout = new QHBoxLayout{titleBar};
        titleLayout->setContentsMargins(8, 2, 4, 2);
        titleLayout->setSpacing(4);
        auto* titleLabel = new QLabel{"Inspector", titleBar};
        titleLabel->setStyleSheet("color: #e8e8e8; font-size: 11px; font-weight: bold;");
        titleLayout->addWidget(titleLabel);
        titleLayout->addStretch();
        titleLayout->addWidget(lockBtn);
        inspectorDock->setTitleBarWidget(titleBar);

        m_assetBrowser = new AssetBrowserPanel{m_editorUndo, this};
        auto* assetDock = new QDockWidget{"Assets", this};
        assetDock->setWidget(m_assetBrowser);
        addDockWidget(Qt::BottomDockWidgetArea, assetDock);
        m_docks.append(assetDock);

        m_console = new ConsolePanel{this};
        auto* consoleDock = new QDockWidget{"Console", this};
        consoleDock->setWidget(m_console);
        addDockWidget(Qt::BottomDockWidgetArea, consoleDock);
        tabifyDockWidget(assetDock, consoleDock);
        assetDock->raise();
        m_docks.append(consoleDock);

        m_viewport = new VulkanViewportWidget{this};

        m_centralTabs = new QTabWidget{this};
        m_centralTabs->setTabPosition(QTabWidget::North);
        m_centralTabs->setDocumentMode(true);
        m_centralTabs->setTabsClosable(true);
        m_centralTabs->addTab(m_viewport, "Viewport");

        // Viewport tab is not closable
        m_centralTabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);

        connect(m_centralTabs, &QTabWidget::currentChanged, this, &ForgeMainWindow::OnCentralTabChanged);
        connect(m_centralTabs, &QTabWidget::tabCloseRequested, this, &ForgeMainWindow::OnCentralTabClosed);

        m_centralTabs->setStyleSheet(R"(
            QTabWidget::pane { border: none; }
            QTabBar { background: #0d0d0d; border: none; }
            QTabBar::tab {
                background: transparent; color: #666; border: none;
                padding: 8px 24px; margin: 0px 1px;
                border-bottom: 2px solid transparent;
            }
            QTabBar::tab:selected { color: #f0a500; border-bottom: 2px solid #f0a500; }
            QTabBar::tab:hover:!selected { color: #aaa; }
        )");
        setCentralWidget(m_centralTabs);
    }

    void ForgeMainWindow::ConnectSignals()
    {
        connect(m_hierarchy, &HierarchyPanel::entitySelected, this, [this](uint32_t) {
            if (!m_inspectorLocked && m_world && m_registry)
                m_inspector->Refresh(*m_world, *m_selection, *m_registry, *m_editorUndo);
        });

        connect(m_hierarchy, &HierarchyPanel::sceneModified, this, [this] {
            RefreshAll();
            emit sceneModified();
        });

        connect(m_inspector, &InspectorPanel::sceneModified, this, [this] {
            if (m_world && m_hierarchy)
                m_hierarchy->Refresh(*m_world);
            emit sceneModified();
        });

        connect(m_inspector, &InspectorPanel::componentsChanged, this, [this] { RefreshAll(); });

        connect(m_inspector, &InspectorPanel::entityLabelChanged, this,
                [this](queen::Entity entity) { m_hierarchy->RefreshEntityItem(entity); });

        connect(m_inspector, &InspectorPanel::browseToAsset, this, [this](const QString& path) {
            m_assetBrowser->NavigateToFile(std::filesystem::path{path.toStdString()});
        });

        connect(m_assetBrowser, &AssetBrowserPanel::assetSelected, this, [this](const QString& path, AssetType type) {
            if (m_inspectorLocked)
                return;
            if (m_selection)
            {
                m_selection->SelectAsset(std::filesystem::path{path.toStdString()}, type);
                if (m_world && m_registry)
                    m_inspector->Refresh(*m_world, *m_selection, *m_registry, *m_editorUndo);
            }
        });

        connect(m_assetBrowser, &AssetBrowserPanel::gltfImportRequested, this, &ForgeMainWindow::gltfImportRequested);
        connect(m_toolbar, &EditorToolbar::buildPressed, this, &ForgeMainWindow::buildRequested);

        connect(m_assetBrowser, &AssetBrowserPanel::sceneOpenRequested, this, &ForgeMainWindow::sceneOpenRequested);
        connect(m_assetBrowser, &AssetBrowserPanel::assetRenamed, this, &ForgeMainWindow::assetRenamed);
        connect(m_assetBrowser, &AssetBrowserPanel::assetDeleted, this, &ForgeMainWindow::assetDeleted);

        connect(m_assetBrowser, &AssetBrowserPanel::assetImported, this, [this](const QString& path) {
            std::filesystem::path fsPath{path.toStdString()};
            auto ext = fsPath.extension().string();
            if (ext == ".gltf" || ext == ".glb")
            {
                hive::LogInfo(LOG_FORGE, "Imported: {}", path.toStdString());
            }
            RefreshAll();
        });

        connect(m_hierarchy, &HierarchyPanel::assetDropped, this, [this](const QString& path) {
            if (!m_world)
                return;

            std::filesystem::path fsPath{path.toStdString()};
            const auto ext = fsPath.extension().string();
            if (ext != ".nmsh")
            {
                hive::LogWarning(LOG_FORGE, "Unsupported asset drop: {}", path.toStdString());
                return;
            }

            const std::filesystem::path assetsRoot =
                m_assetBrowser != nullptr ? m_assetBrowser->Root() : std::filesystem::path{};
            SpawnNmshEntities(*m_world, fsPath, assetsRoot);

            RefreshAll();
            emit m_hierarchy->sceneModified();
        });

        connect(m_assetBrowser, &AssetBrowserPanel::assetOpenRequested, this,
                [this](const QString& path, AssetType type) {
                    if (type == AssetType::BLUEPRINT)
                    {
                        OpenBlueprint(path);
                    }
                });

        connect(m_blueprintOutline, &BlueprintOutlinePanel::variableSelected, this,
                [this](uint32_t varId) { m_blueprintInspector->InspectVariable(varId); });
        connect(m_blueprintOutline, &BlueprintOutlinePanel::nothingSelected, this,
                [this]() { m_blueprintInspector->ShowGraphProperties(); });
        connect(m_blueprintInspector, &BlueprintNodeInspector::graphModified, this,
                [this]() { m_blueprintOutline->Refresh(); });
    }

    void ForgeMainWindow::SwitchToViewportMode()
    {
        m_leftStack->setCurrentIndex(0);
        m_rightStack->setCurrentIndex(0);
        m_leftDock->setWindowTitle("Hierarchy");
        m_blueprintOutline->SetEditor(nullptr);
        m_blueprintInspector->SetEditor(nullptr);
    }

    void ForgeMainWindow::SwitchToBlueprintMode(BlueprintPanel* panel)
    {
        if (!panel)
        {
            return;
        }

        auto* editor = panel->Editor();
        m_blueprintOutline->SetEditor(editor);
        m_blueprintInspector->SetEditor(editor);

        disconnect(editor, &BlueprintEditor::nodeSelected, m_blueprintInspector, &BlueprintNodeInspector::InspectNode);
        disconnect(editor, &BlueprintEditor::selectionCleared, m_blueprintInspector,
                   &BlueprintNodeInspector::ShowGraphProperties);

        connect(editor, &BlueprintEditor::nodeSelected, m_blueprintInspector, &BlueprintNodeInspector::InspectNode);
        connect(editor, &BlueprintEditor::selectionCleared, m_blueprintInspector,
                &BlueprintNodeInspector::ShowGraphProperties);

        m_leftStack->setCurrentIndex(1);
        m_rightStack->setCurrentIndex(1);
        m_blueprintInspector->Clear();
        m_leftDock->setWindowTitle("My Blueprint");
    }

    void ForgeMainWindow::OnCentralTabChanged(int index)
    {
        if (index < 0)
        {
            return;
        }

        auto* widget = m_centralTabs->widget(index);
        if (widget == m_viewport)
        {
            SwitchToViewportMode();
        }
        else
        {
            auto* panel = qobject_cast<BlueprintPanel*>(widget);
            if (panel)
            {
                SwitchToBlueprintMode(panel);
            }
        }
    }

    void ForgeMainWindow::OnCentralTabClosed(int index)
    {
        auto* widget = m_centralTabs->widget(index);
        if (widget == m_viewport)
        {
            return;
        }

        auto* panel = qobject_cast<BlueprintPanel*>(widget);
        if (panel && panel->IsDirty())
        {
            if (!panel->PromptSaveIfDirty())
            {
                return;
            }
        }

        m_centralTabs->removeTab(index);
        widget->deleteLater();

        if (m_centralTabs->currentWidget() == m_viewport)
        {
            SwitchToViewportMode();
        }
    }

    BlueprintPanel* ForgeMainWindow::ActiveBlueprintPanel()
    {
        return qobject_cast<BlueprintPanel*>(m_centralTabs->currentWidget());
    }

    bool ForgeMainWindow::PromptAllDirtyBlueprints()
    {
        for (int i = 0; i < m_centralTabs->count(); ++i)
        {
            auto* panel = qobject_cast<BlueprintPanel*>(m_centralTabs->widget(i));
            if (panel && panel->IsDirty())
            {
                m_centralTabs->setCurrentIndex(i);
                if (!panel->PromptSaveIfDirty())
                {
                    return false;
                }
            }
        }
        return true;
    }

    void ForgeMainWindow::OpenBlueprint(const QString& filePath)
    {
        // Check if already open — activate existing tab
        for (int i = 0; i < m_centralTabs->count(); ++i)
        {
            auto* panel = qobject_cast<BlueprintPanel*>(m_centralTabs->widget(i));
            if (panel && panel->FilePath() == filePath)
            {
                m_centralTabs->setCurrentIndex(i);
                return;
            }
        }

        auto* panel = new BlueprintPanel{this};
        panel->Editor()->SetUndoManager(m_editorUndo);
        queen::World* world = m_world;
        panel->Editor()->SetFunctionRegistryProvider([world]() -> const propolis::FunctionRegistry* {
            return world ? world->Resource<propolis::FunctionRegistry>() : nullptr;
        });
        if (!filePath.isEmpty())
        {
            panel->LoadFromFile(filePath);
        }

        QFileInfo info{filePath};
        QString tabName = info.baseName().isEmpty() ? "Untitled" : info.baseName();
        int tabIndex = m_centralTabs->addTab(panel, tabName);
        m_centralTabs->setCurrentIndex(tabIndex);

        connect(panel, &BlueprintPanel::dirtyChanged, this, [this, panel](bool dirty) {
            int idx = m_centralTabs->indexOf(panel);
            if (idx < 0)
            {
                return;
            }
            QString base = panel->FilePath().isEmpty() ? "Untitled" : QFileInfo{panel->FilePath()}.baseName();
            m_centralTabs->setTabText(idx, dirty ? base + " *" : base);
        });
    }

    void ForgeMainWindow::ShowProgress(const QString& title)
    {
        if (!m_progressOverlay)
            m_progressOverlay = new ProgressOverlay{this};
        m_progressOverlay->Show(title);
    }

    void ForgeMainWindow::ProgressSetStep(const QString& step)
    {
        if (m_progressOverlay)
            m_progressOverlay->SetStep(step);
    }

    void ForgeMainWindow::ProgressSetProgress(int current, int total)
    {
        if (m_progressOverlay)
            m_progressOverlay->SetProgress(current, total);
    }

    void ForgeMainWindow::HideProgress()
    {
        if (m_progressOverlay)
            m_progressOverlay->Hide();
    }

    void ForgeMainWindow::ShowBootProgress(const QString& projectName)
    {
        if (m_hub != nullptr)
        {
            m_hub->hide();
        }
        if (m_loadingWidget != nullptr)
        {
            m_loadingWidget->hide();
            m_loadingWidget = nullptr;
        }
        menuBar()->hide();
        setWindowTitle(QString{"HiveEngine - %1"}.arg(projectName));

        if (m_progressOverlay == nullptr)
        {
            m_progressOverlay = new ProgressOverlay{this};
        }
        m_progressOverlay->Show(QString{"Loading %1"}.arg(projectName));
    }

    void ForgeMainWindow::BootProgressSetStep(const QString& step)
    {
        if (m_progressOverlay != nullptr)
        {
            m_progressOverlay->SetStep(step);
        }
    }

    void ForgeMainWindow::BootProgressSetProgress(int current, int total)
    {
        if (m_progressOverlay != nullptr)
        {
            m_progressOverlay->SetProgress(current, total);
        }
    }

    void ForgeMainWindow::HideBootProgress()
    {
        if (m_progressOverlay != nullptr)
        {
            m_progressOverlay->Hide();
        }
    }
} // namespace forge
