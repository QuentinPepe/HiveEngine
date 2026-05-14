#include <wax/containers/fixed_string.h>

#include <queen/core/type_id.h>
#include <queen/reflect/component_registry.h>
#include <queen/world/world.h>

#include <propolis/runtime/propolis_script.h>
#include <propolis/runtime/script_registry.h>

#include <waggle/components/disabled.h>
#include <waggle/components/name.h>
#include <waggle/disabled_propagation.h>

#include <forge/add_component_popup.h>
#include <forge/editor_undo.h>
#include <forge/entity_inspector.h>
#include <forge/entity_inspector_helpers.h>
#include <forge/inspector_panel.h>
#include <forge/selection.h>

#include <QCheckBox>
#include <QCursor>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace forge
{
    namespace
    {
        struct ComponentBuffer
        {
            std::byte* m_data{nullptr};
            size_t m_size{0};
            size_t m_alignment{0};

            ComponentBuffer(size_t size, size_t alignment)
                : m_size{size}
                , m_alignment{alignment != 0 ? alignment : alignof(std::max_align_t)}
            {
                m_data = static_cast<std::byte*>(::operator new(m_size, std::align_val_t{m_alignment}));
            }

            ~ComponentBuffer()
            {
                if (m_data != nullptr)
                    ::operator delete(m_data, m_size, std::align_val_t{m_alignment});
            }

            ComponentBuffer(const ComponentBuffer&) = delete;
            ComponentBuffer& operator=(const ComponentBuffer&) = delete;
            ComponentBuffer(ComponentBuffer&&) = delete;
            ComponentBuffer& operator=(ComponentBuffer&&) = delete;
        };
    } // namespace

    EntityInspector::EntityInspector(queen::World& world, EditorSelection& selection,
                                     const queen::ComponentRegistry<256>& registry, EditorUndoManager& editorUndo,
                                     QWidget* parent)
        : QWidget{parent}
        , m_world{&world}
        , m_registry{&registry}
        , m_undo{&editorUndo}
    {
        m_rootLayout = new QVBoxLayout{this};
        m_rootLayout->setContentsMargins(4, 2, 4, 4);
        m_rootLayout->setSpacing(1);

        if (selection.All().Size() > 1)
        {
            const auto& all = selection.All();
            m_multiEntities.Reserve(all.Size());
            for (size_t i = 0; i < all.Size(); ++i)
                m_multiEntities.PushBack(all[i]);
            BuildMultiEntity(world, selection.All(), registry, editorUndo);
        }
        else
        {
            m_singleEntity = selection.Primary();
            BuildSingleEntity(world, selection.Primary(), registry, editorUndo);
        }
        m_rootLayout->addStretch(1);
    }

    void EntityInspector::BuildSingleEntity(queen::World& world, queen::Entity entity,
                                            const queen::ComponentRegistry<256>& registry,
                                            EditorUndoManager& editorUndo)
    {
        if (entity.IsNull() || !world.IsAlive(entity))
        {
            return;
        }

        constexpr queen::TypeId nameTypeId = queen::TypeIdOf<waggle::Name>();

        auto* nameData = static_cast<waggle::Name*>(world.GetComponentRaw(entity, nameTypeId));

        auto* headerRow = new QHBoxLayout;
        headerRow->setContentsMargins(2, 2, 2, 4);
        headerRow->setSpacing(4);

        auto* enableCheck = new QCheckBox;
        enableCheck->setChecked(!world.Has<waggle::Disabled>(entity));
        enableCheck->setToolTip("Enable/Disable entity");
        headerRow->addWidget(enableCheck);

        auto* nameEdit = new QLineEdit{
            nameData ? QString::fromUtf8(nameData->m_name.CStr(), static_cast<int>(nameData->m_name.Size()))
                     : QString{}};
        nameEdit->setObjectName("inspectorHeader");
        nameEdit->setPlaceholderText(QString{"Entity %1"}.arg(entity.Index()));
        headerRow->addWidget(nameEdit);

        m_rootLayout->addLayout(headerRow);

        QObject::connect(
            enableCheck, &QCheckBox::checkStateChanged, this,
            [this, &world, &editorUndo, entity](Qt::CheckState state) {
                bool disabling = (state != Qt::Checked);
                waggle::SetEntityDisabled(world, entity, disabling);
                editorUndo.Push(
                    [&world, entity, disabling]() {
                        waggle::SetEntityDisabled(world, entity, !disabling);
                    },
                    [&world, entity, disabling]() {
                        waggle::SetEntityDisabled(world, entity, disabling);
                    });
                emit entityLabelChanged(entity);
                emit sceneModified();
            });

        if (!nameData)
        {
            nameEdit->setReadOnly(true);
        }
        else
        {
            auto snapshot = std::make_shared<SnapshotState>();
            auto snapshotTaken = std::make_shared<bool>(false);

            QObject::connect(
                nameEdit, &QLineEdit::textEdited, this,
                [this, nameData, snapshot, snapshotTaken, entity](const QString& text) {
                    if (!*snapshotTaken)
                    {
                        Snapshot(*snapshot, entity, nameTypeId, 0,
                                 static_cast<uint16_t>(sizeof(wax::FixedString)),
                                 &nameData->m_name);
                        *snapshotTaken = true;
                    }
                    QByteArray utf8 = text.toUtf8();
                    nameData->m_name =
                        wax::FixedString{utf8.constData(), static_cast<size_t>(utf8.size())};
                    emit entityLabelChanged(entity);
                });

            QObject::connect(
                nameEdit, &QLineEdit::editingFinished, this,
                [this, nameData, snapshot, snapshotTaken, &world, &editorUndo]() {
                    if (*snapshotTaken)
                    {
                        CommitIfChanged(*snapshot, editorUndo, world, &nameData->m_name);
                        *snapshotTaken = false;
                        emit sceneModified();
                    }
                });
        }

        wax::Vector<queen::TypeId> componentTypes;
        world.ForEachComponentType(entity,
                                   [&](queen::TypeId typeId) { componentTypes.PushBack(typeId); });

        for (size_t typeIndex = 0; typeIndex < componentTypes.Size(); ++typeIndex)
        {
            const queen::TypeId typeId = componentTypes[typeIndex];
            if (typeId == nameTypeId)
                continue;
            const auto* reg = registry.Find(typeId);
            if (reg == nullptr || !reg->HasReflection())
                continue;
            if (reg->m_reflection.m_category == queen::ComponentCategory::INTERNAL)
                continue;
            void* comp = world.GetComponentRaw(entity, typeId);
            if (comp == nullptr)
                continue;
            const auto& reflection = reg->m_reflection;
            const QString title = MakeComponentGroupTitle(typeId, reflection.m_name);

            auto* group = new QGroupBox{title};
            group->setCheckable(true);
            group->setChecked(true);

            auto* outer = new QVBoxLayout{group};
            outer->setContentsMargins(4, 4, 4, 4);
            outer->setSpacing(2);

            auto* menuRow = new QHBoxLayout;
            menuRow->setContentsMargins(0, 0, 0, 0);
            menuRow->addStretch(1);
            auto* menuButton = new QToolButton{group};
            menuButton->setText(QStringLiteral("⋮"));
            menuButton->setToolTip(QStringLiteral("Component actions"));
            menuButton->setAutoRaise(true);
            menuButton->setCursor(Qt::PointingHandCursor);
            menuButton->setStyleSheet(
                QStringLiteral("QToolButton { color: #888; border: none; padding: 0 4px; }"
                               "QToolButton:hover { color: #f0a500; }"));
            QObject::connect(menuButton, &QToolButton::clicked, this, [this, typeId, menuButton]() {
                QMenu menu{menuButton};
                menu.addAction(QStringLiteral("Remove Component"), this,
                               [this, typeId]() { HandleRemoveComponentClicked(typeId); });
                menu.exec(QCursor::pos());
            });
            menuRow->addWidget(menuButton);
            outer->addLayout(menuRow);

            auto* form = new QFormLayout;
            form->setContentsMargins(0, 0, 0, 0);
            form->setSpacing(3);
            form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

            const bool isScript = (typeId == queen::TypeIdOf<propolis::PropolisScript>());

            FieldContext ctx{&world, entity, typeId, 0, &editorUndo};
            for (size_t i = 0; i < reflection.m_fieldCount; ++i)
            {
                if (isScript)
                    continue;
                BuildFieldWidget(reflection.m_fields[i], comp, ctx, form);
            }
            outer->addLayout(form);

            m_rootLayout->addWidget(group);
        }

        auto* addButton = new QPushButton{QStringLiteral("+ Add Component")};
        addButton->setStyleSheet(
            QStringLiteral("QPushButton { color: #e8e8e8; background: #1a1a1a; border: 1px dashed #444;"
                           " border-radius: 4px; padding: 6px; font-size: 11px; margin: 6px 8px; }"
                           "QPushButton:hover { border-color: #f0a500; color: #f0a500; }"));
        QObject::connect(addButton, &QPushButton::clicked, this,
                         [this, addButton]() { HandleAddComponentClicked(addButton); });
        m_rootLayout->addWidget(addButton);
    }

    InspectorPanel* EntityInspector::FindInspectorPanel()
    {
        if (m_inspectorPanel != nullptr)
            return m_inspectorPanel;
        for (QWidget* widget = parentWidget(); widget != nullptr; widget = widget->parentWidget())
        {
            m_inspectorPanel = qobject_cast<InspectorPanel*>(widget);
            if (m_inspectorPanel != nullptr)
                return m_inspectorPanel;
        }
        return nullptr;
    }

    QString EntityInspector::MakeComponentGroupTitle(queen::TypeId typeId, const char* fallbackName)
    {
        const char* raw = fallbackName != nullptr ? fallbackName : "Component";
        const char* shortName = raw;
        if (const char* sep = std::strrchr(raw, ':'))
            shortName = sep + 1;

        if (m_world != nullptr && typeId == queen::TypeIdOf<propolis::PropolisScript>())
        {
            const auto* script = static_cast<const propolis::PropolisScript*>(
                m_world->GetComponentRaw(m_singleEntity, typeId));
            const auto* scripts = m_world->Resource<propolis::ScriptRegistry>();
            if (script != nullptr && scripts != nullptr)
            {
                if (const auto* entry = scripts->FindByHash(script->m_nameHash))
                {
                    return QStringLiteral("Script: ") + QString::fromUtf8(entry->m_name.CStr());
                }
            }
            char hashBuf[32];
            std::snprintf(hashBuf, sizeof(hashBuf), "Script: 0x%08X",
                          script != nullptr ? script->m_nameHash : 0u);
            return QString::fromUtf8(hashBuf);
        }

        return QString::fromUtf8(shortName);
    }

    void EntityInspector::HandleAddComponentClicked(QWidget* anchor)
    {
        if (m_world == nullptr || m_registry == nullptr || m_undo == nullptr || m_singleEntity.IsNull())
            return;
        if (!m_world->IsAlive(m_singleEntity))
            return;

        const QPoint pos = anchor->mapToGlobal(QPoint{0, anchor->height()});
        const queen::Entity entity = m_singleEntity;
        queen::World& world = *m_world;
        EditorUndoManager& undo = *m_undo;
        const auto& registry = *m_registry;

        // onAccept may trigger an inspector rebuild that destroys this EntityInspector
        // before the lambda finishes. Capture only stable refs (registry, world, undo,
        // and the long-lived InspectorPanel) — never `this`.
        InspectorPanel* panel = FindInspectorPanel();

        AddComponentPopup::Show(
            anchor, pos, registry, world, entity,
            [&registry, &world, &undo, entity, panel](const AddComponentChoice& choice) {
                if (choice.m_kind == AddComponentKind::COMPONENT)
                {
                    const auto* reg = registry.Find(choice.m_componentTypeId);
                    if (reg == nullptr || reg->m_meta.m_construct == nullptr)
                        return;
                    const auto& meta = reg->m_meta;
                    auto buffer = std::make_shared<ComponentBuffer>(meta.m_size, meta.m_alignment);
                    meta.m_construct(buffer->m_data);
                    world.AddRaw(entity, meta, buffer->m_data);

                    const queen::TypeId typeId = meta.m_typeId;
                    undo.Push(
                        [&world, entity, typeId]() { world.RemoveByTypeId(entity, typeId); },
                        [&world, entity, meta, buffer]() {
                            world.AddRaw(entity, meta, buffer->m_data);
                        });
                }
                else
                {
                    propolis::PropolisScript script{choice.m_scriptNameHash, nullptr};
                    const auto* reg = registry.Find(queen::TypeIdOf<propolis::PropolisScript>());
                    if (reg == nullptr)
                        return;
                    const auto& meta = reg->m_meta;
                    world.AddRaw(entity, meta, &script);

                    const uint32_t hash = choice.m_scriptNameHash;
                    const queen::TypeId typeId = meta.m_typeId;
                    undo.Push(
                        [&world, entity, typeId]() { world.RemoveByTypeId(entity, typeId); },
                        [&world, entity, meta, hash]() {
                            propolis::PropolisScript replay{hash, nullptr};
                            world.AddRaw(entity, meta, &replay);
                        });
                }

                if (panel != nullptr)
                    emit panel->componentsChanged();
            });
    }

    void EntityInspector::HandleRemoveComponentClicked(queen::TypeId typeId)
    {
        if (m_world == nullptr || m_registry == nullptr || m_undo == nullptr || m_singleEntity.IsNull())
            return;
        if (!m_world->IsAlive(m_singleEntity))
            return;

        const auto* reg = m_registry->Find(typeId);
        if (reg == nullptr)
            return;
        const auto& meta = reg->m_meta;
        const queen::Entity entity = m_singleEntity;
        queen::World& world = *m_world;
        EditorUndoManager& undo = *m_undo;

        const bool isScript = (typeId == queen::TypeIdOf<propolis::PropolisScript>());
        if (isScript)
        {
            const auto* script =
                static_cast<const propolis::PropolisScript*>(world.GetComponentRaw(entity, typeId));
            const uint32_t hash = script != nullptr ? script->m_nameHash : 0u;
            world.RemoveByTypeId(entity, typeId);

            undo.Push(
                [&world, entity, meta, hash]() {
                    propolis::PropolisScript replay{hash, nullptr};
                    world.AddRaw(entity, meta, &replay);
                },
                [&world, entity, typeId]() { world.RemoveByTypeId(entity, typeId); });
        }
        else
        {
            void* current = world.GetComponentRaw(entity, typeId);
            if (current == nullptr)
                return;

            auto buffer = std::make_shared<ComponentBuffer>(meta.m_size, meta.m_alignment);
            std::memcpy(buffer->m_data, current, meta.m_size);
            world.RemoveByTypeId(entity, typeId);

            undo.Push(
                [&world, entity, meta, buffer]() { world.AddRaw(entity, meta, buffer->m_data); },
                [&world, entity, typeId]() { world.RemoveByTypeId(entity, typeId); });
        }

        emit sceneModified();
        emit componentsChanged();
    }

    void EntityInspector::HandleMultiAddComponentClicked(QWidget* anchor)
    {
        if (m_world == nullptr || m_registry == nullptr || m_undo == nullptr || m_multiEntities.IsEmpty())
            return;

        const QPoint pos = anchor->mapToGlobal(QPoint{0, anchor->height()});
        queen::World& world = *m_world;
        EditorUndoManager& undo = *m_undo;
        const auto& registry = *m_registry;
        InspectorPanel* panel = FindInspectorPanel();

        // Deep-copy the selection so the popup callback survives an inspector rebuild
        // (m_multiEntities would dangle if this EntityInspector gets destroyed before
        // the user picks).
        auto selectionSnapshot = std::make_shared<wax::Vector<queen::Entity>>();
        selectionSnapshot->Reserve(m_multiEntities.Size());
        for (size_t i = 0; i < m_multiEntities.Size(); ++i)
            selectionSnapshot->PushBack(m_multiEntities[i]);

        AddComponentPopup::ShowForEntities(
            anchor, pos, registry, world, *selectionSnapshot,
            [&registry, &world, &undo, selectionSnapshot, panel](const AddComponentChoice& choice) {
                const auto& entities = *selectionSnapshot;
                auto targets = std::make_shared<wax::Vector<queen::Entity>>();

                if (choice.m_kind == AddComponentKind::COMPONENT)
                {
                    const auto* reg = registry.Find(choice.m_componentTypeId);
                    if (reg == nullptr || reg->m_meta.m_construct == nullptr)
                        return;
                    const auto& meta = reg->m_meta;
                    auto buffer = std::make_shared<ComponentBuffer>(meta.m_size, meta.m_alignment);
                    meta.m_construct(buffer->m_data);

                    for (size_t i = 0; i < entities.Size(); ++i)
                    {
                        const queen::Entity entity = entities[i];
                        if (!world.IsAlive(entity) || world.HasComponent(entity, meta.m_typeId))
                            continue;
                        world.AddRaw(entity, meta, buffer->m_data);
                        targets->PushBack(entity);
                    }
                    if (targets->IsEmpty())
                        return;

                    const queen::TypeId typeId = meta.m_typeId;
                    undo.Push(
                        [&world, targets, typeId]() {
                            for (size_t i = 0; i < targets->Size(); ++i)
                                world.RemoveByTypeId((*targets)[i], typeId);
                        },
                        [&world, targets, meta, buffer]() {
                            for (size_t i = 0; i < targets->Size(); ++i)
                                world.AddRaw((*targets)[i], meta, buffer->m_data);
                        });
                }
                else
                {
                    const auto* reg = registry.Find(queen::TypeIdOf<propolis::PropolisScript>());
                    if (reg == nullptr)
                        return;
                    const auto& meta = reg->m_meta;
                    const uint32_t hash = choice.m_scriptNameHash;
                    for (size_t i = 0; i < entities.Size(); ++i)
                    {
                        const queen::Entity entity = entities[i];
                        if (!world.IsAlive(entity) || world.HasComponent(entity, meta.m_typeId))
                            continue;
                        propolis::PropolisScript script{hash, nullptr};
                        world.AddRaw(entity, meta, &script);
                        targets->PushBack(entity);
                    }
                    if (targets->IsEmpty())
                        return;

                    const queen::TypeId typeId = meta.m_typeId;
                    undo.Push(
                        [&world, targets, typeId]() {
                            for (size_t i = 0; i < targets->Size(); ++i)
                                world.RemoveByTypeId((*targets)[i], typeId);
                        },
                        [&world, targets, meta, hash]() {
                            for (size_t i = 0; i < targets->Size(); ++i)
                            {
                                propolis::PropolisScript replay{hash, nullptr};
                                world.AddRaw((*targets)[i], meta, &replay);
                            }
                        });
                }

                if (panel != nullptr)
                    emit panel->componentsChanged();
            });
    }

    void EntityInspector::HandleMultiRemoveComponentClicked(queen::TypeId typeId)
    {
        if (m_world == nullptr || m_registry == nullptr || m_undo == nullptr || m_multiEntities.IsEmpty())
            return;

        const auto* reg = m_registry->Find(typeId);
        if (reg == nullptr)
            return;
        const auto& meta = reg->m_meta;
        queen::World& world = *m_world;
        EditorUndoManager& undo = *m_undo;
        const bool isScript = (typeId == queen::TypeIdOf<propolis::PropolisScript>());

        struct RemovedEntry
        {
            queen::Entity m_entity{};
            std::shared_ptr<ComponentBuffer> m_buffer{};
            uint32_t m_scriptHash{0};
        };
        auto removed = std::make_shared<wax::Vector<RemovedEntry>>();

        for (size_t i = 0; i < m_multiEntities.Size(); ++i)
        {
            const queen::Entity entity = m_multiEntities[i];
            if (!world.IsAlive(entity) || !world.HasComponent(entity, typeId))
                continue;

            RemovedEntry entry{};
            entry.m_entity = entity;
            if (isScript)
            {
                const auto* script =
                    static_cast<const propolis::PropolisScript*>(world.GetComponentRaw(entity, typeId));
                entry.m_scriptHash = script != nullptr ? script->m_nameHash : 0u;
            }
            else
            {
                void* current = world.GetComponentRaw(entity, typeId);
                if (current == nullptr)
                    continue;
                entry.m_buffer = std::make_shared<ComponentBuffer>(meta.m_size, meta.m_alignment);
                std::memcpy(entry.m_buffer->m_data, current, meta.m_size);
            }
            world.RemoveByTypeId(entity, typeId);
            removed->PushBack(std::move(entry));
        }

        if (removed->IsEmpty())
            return;

        undo.Push(
            [&world, removed, meta, isScript]() {
                for (size_t i = 0; i < removed->Size(); ++i)
                {
                    const auto& entry = (*removed)[i];
                    if (isScript)
                    {
                        propolis::PropolisScript replay{entry.m_scriptHash, nullptr};
                        world.AddRaw(entry.m_entity, meta, &replay);
                    }
                    else
                    {
                        world.AddRaw(entry.m_entity, meta, entry.m_buffer->m_data);
                    }
                }
            },
            [&world, removed, typeId]() {
                for (size_t i = 0; i < removed->Size(); ++i)
                    world.RemoveByTypeId((*removed)[i].m_entity, typeId);
            });

        emit sceneModified();
        emit componentsChanged();
    }

    void EntityInspector::BuildMultiEntity(queen::World& world,
                                           const wax::Vector<queen::Entity>& entities,
                                           const queen::ComponentRegistry<256>& registry,
                                           EditorUndoManager& editorUndo)
    {
        constexpr queen::TypeId nameTypeId = queen::TypeIdOf<waggle::Name>();

        auto* headerRow = new QHBoxLayout;
        headerRow->setContentsMargins(2, 2, 2, 4);
        headerRow->setSpacing(4);

        bool allDisabled = true;
        for (size_t i = 0; i < entities.Size(); ++i)
        {
            if (!world.Has<waggle::Disabled>(entities[i]))
            {
                allDisabled = false;
                break;
            }
        }

        auto* enableCheck = new QCheckBox;
        enableCheck->setChecked(!allDisabled);
        enableCheck->setToolTip("Enable/Disable entities");
        headerRow->addWidget(enableCheck);

        auto* header = new QLabel{QString{"%1 entities selected"}.arg(entities.Size())};
        header->setObjectName("inspectorHeader");
        headerRow->addWidget(header);

        m_rootLayout->addLayout(headerRow);

        QObject::connect(
            enableCheck, &QCheckBox::clicked, this,
            [this, &world, &editorUndo, entities = &entities](bool checked) {
                bool disabling = !checked;
                auto before =
                    std::make_shared<std::vector<std::pair<queen::Entity, bool>>>();
                for (size_t i = 0; i < entities->Size(); ++i)
                {
                    before->push_back(
                        {(*entities)[i], world.Has<waggle::Disabled>((*entities)[i])});
                    waggle::SetEntityDisabled(world, (*entities)[i], disabling);
                }
                editorUndo.Push(
                    [&world, before]() {
                        for (auto& [e, wasDisabled] : *before)
                            waggle::SetEntityDisabled(world, e, wasDisabled);
                    },
                    [&world, before, disabling]() {
                        for (auto& [e, _] : *before)
                            waggle::SetEntityDisabled(world, e, disabling);
                    });
                emit sceneModified();
            });

        queen::Entity primary = entities[0];
        if (primary.IsNull() || !world.IsAlive(primary))
            return;

        std::vector<queen::TypeId> commonTypes;
        world.ForEachComponentType(primary, [&](queen::TypeId typeId) {
            if (typeId == nameTypeId)
                return;
            for (size_t i = 0; i < entities.Size(); ++i)
            {
                if (!world.HasComponent(entities[i], typeId))
                    return;
            }
            commonTypes.push_back(typeId);
        });

        for (auto typeId : commonTypes)
        {
            const auto* reg = registry.Find(typeId);
            if (reg == nullptr || !reg->HasReflection())
                continue;
            if (reg->m_reflection.m_category == queen::ComponentCategory::INTERNAL)
                continue;
            void* primaryComp = world.GetComponentRaw(primary, typeId);
            if (primaryComp == nullptr)
                continue;

            const auto& reflection = reg->m_reflection;
            const char* rawName = reflection.m_name != nullptr ? reflection.m_name : "Component";
            const char* name = rawName;
            if (const char* sep = std::strrchr(rawName, ':'))
                name = sep + 1;

            auto* group = new QGroupBox{QString::fromUtf8(name)};
            group->setCheckable(true);
            group->setChecked(true);

            auto* outer = new QVBoxLayout{group};
            outer->setContentsMargins(4, 4, 4, 4);
            outer->setSpacing(2);

            auto* menuRow = new QHBoxLayout;
            menuRow->setContentsMargins(0, 0, 0, 0);
            menuRow->addStretch(1);
            auto* menuButton = new QToolButton{group};
            menuButton->setText(QStringLiteral("⋮"));
            menuButton->setToolTip(QStringLiteral("Component actions"));
            menuButton->setAutoRaise(true);
            menuButton->setCursor(Qt::PointingHandCursor);
            menuButton->setStyleSheet(
                QStringLiteral("QToolButton { color: #888; border: none; padding: 0 4px; }"
                               "QToolButton:hover { color: #f0a500; }"));
            QObject::connect(menuButton, &QToolButton::clicked, this, [this, typeId, menuButton]() {
                QMenu menu{menuButton};
                menu.addAction(QStringLiteral("Remove from all"), this,
                               [this, typeId]() { HandleMultiRemoveComponentClicked(typeId); });
                menu.exec(QCursor::pos());
            });
            menuRow->addWidget(menuButton);
            outer->addLayout(menuRow);

            auto* form = new QFormLayout;
            form->setContentsMargins(0, 0, 0, 0);
            form->setSpacing(3);
            form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

            MultiFieldContext ctx{&world, &entities, typeId, 0, &editorUndo};
            for (size_t i = 0; i < reflection.m_fieldCount; ++i)
                BuildMultiFieldWidget(reflection.m_fields[i], primaryComp, ctx, form);
            outer->addLayout(form);

            m_rootLayout->addWidget(group);
        }

        auto* addButton = new QPushButton{QStringLiteral("+ Add Component")};
        addButton->setStyleSheet(
            QStringLiteral("QPushButton { color: #e8e8e8; background: #1a1a1a; border: 1px dashed #444;"
                           " border-radius: 4px; padding: 6px; font-size: 11px; margin: 6px 8px; }"
                           "QPushButton:hover { border-color: #f0a500; color: #f0a500; }"));
        QObject::connect(addButton, &QPushButton::clicked, this,
                         [this, addButton]() { HandleMultiAddComponentClicked(addButton); });
        m_rootLayout->addWidget(addButton);
    }
} // namespace forge
