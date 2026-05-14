#include <forge/add_component_popup.h>

#include <propolis/runtime/propolis_script.h>
#include <propolis/runtime/script_registry.h>

#include <queen/world/world.h>

#include <QColor>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QKeyEvent>
#include <QLineEdit>
#include <QObject>
#include <QString>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <cstring>

namespace forge
{
    namespace
    {
        constexpr const char* kPopupStyle = R"(
            QFrame { background: #1a1a1a; border: 1px solid #333; border-radius: 6px; }
            QLineEdit { background: #0d0d0d; color: #e8e8e8; border: 1px solid #444; border-radius: 3px;
                         padding: 6px 8px; font-size: 12px; selection-background-color: #3d2e0a; }
            QTreeWidget { background: transparent; border: none; color: #e8e8e8; font-size: 11px; outline: none; }
            QTreeWidget::item { padding: 3px 8px; border-radius: 2px; }
            QTreeWidget::item:selected { background: #3d2e0a; color: #f0a500; }
            QTreeWidget::item:hover:!selected { background: #252525; }
            QTreeWidget::item:disabled { color: #555; }
        )";

        constexpr int kItemKindRole = Qt::UserRole;
        constexpr int kItemValueRole = Qt::UserRole + 1;

        QString DisplayNameFromReflectionName(const char* rawName)
        {
            if (rawName == nullptr)
                return QStringLiteral("Component");
            const char* last = std::strrchr(rawName, ':');
            return QString::fromUtf8(last != nullptr ? last + 1 : rawName);
        }

        QTreeWidgetItem* MakeHeader(QTreeWidget* tree, const QString& label)
        {
            auto* header = new QTreeWidgetItem{tree, {label}};
            header->setFlags(Qt::ItemIsEnabled);
            QFont f = header->font(0);
            f.setBold(true);
            header->setFont(0, f);
            header->setForeground(0, QColor{0xf0, 0xa5, 0x00});
            return header;
        }

        using IsPresentFn = std::function<bool(queen::TypeId)>;

        void Populate(QTreeWidget* tree, const queen::ComponentRegistry<256>& registry, queen::World& world,
                      const IsPresentFn& isAlreadyPresent, bool allEntitiesHaveScript, const QString& filter)
        {
            tree->clear();
            const QString needle = filter.toLower();
            const bool filtering = !needle.isEmpty();

            auto* compsHeader = MakeHeader(tree, QStringLiteral("Components"));
            bool anyComp = false;
            for (size_t i = 0; i < registry.Count(); ++i)
            {
                const auto& reg = registry[i];
                if (!reg.HasReflection())
                    continue;

                const auto category = reg.m_reflection.m_category;
                if (category != queen::ComponentCategory::USER)
                    continue;

                const queen::TypeId typeId = reg.m_meta.m_typeId;
                if (isAlreadyPresent(typeId))
                    continue;

                const QString label = DisplayNameFromReflectionName(reg.m_reflection.m_name);
                if (filtering && !label.toLower().contains(needle))
                    continue;

                auto* item = new QTreeWidgetItem{compsHeader, {label}};
                item->setData(0, kItemKindRole, static_cast<int>(AddComponentKind::COMPONENT));
                item->setData(0, kItemValueRole, QVariant::fromValue<qulonglong>(typeId));
                anyComp = true;
            }
            if (!anyComp)
            {
                auto* placeholder = new QTreeWidgetItem{compsHeader,
                                                       {QStringLiteral("(no components available)")}};
                placeholder->setFlags(Qt::NoItemFlags);
            }

            auto* scriptsHeader = MakeHeader(tree, QStringLiteral("Scripts"));
            const auto* scripts = world.Resource<propolis::ScriptRegistry>();
            if (scripts == nullptr || scripts->Count() == 0)
            {
                auto* placeholder = new QTreeWidgetItem{scriptsHeader,
                                                       {QStringLiteral("(no scripts registered)")}};
                placeholder->setFlags(Qt::NoItemFlags);
            }
            else if (allEntitiesHaveScript)
            {
                auto* placeholder = new QTreeWidgetItem{scriptsHeader,
                                                       {QStringLiteral("(entity already has a script)")}};
                placeholder->setFlags(Qt::NoItemFlags);
                placeholder->setToolTip(0,
                                        QStringLiteral("Remove the existing script before attaching another."));
            }
            else
            {
                bool anyScript = false;
                const auto& entries = scripts->All();
                for (size_t i = 0; i < entries.Size(); ++i)
                {
                    const QString name = QString::fromUtf8(entries[i].m_name.CStr());
                    if (filtering && !name.toLower().contains(needle))
                        continue;
                    auto* item = new QTreeWidgetItem{scriptsHeader, {name}};
                    item->setData(0, kItemKindRole, static_cast<int>(AddComponentKind::SCRIPT));
                    item->setData(0, kItemValueRole, QVariant::fromValue<uint>(entries[i].m_nameHash));
                    anyScript = true;
                }
                if (!anyScript)
                {
                    auto* placeholder = new QTreeWidgetItem{scriptsHeader, {QStringLiteral("(no matches)")}};
                    placeholder->setFlags(Qt::NoItemFlags);
                }
            }

            tree->expandAll();

            QTreeWidgetItemIterator it{tree};
            while (*it)
            {
                if ((*it)->flags() & Qt::ItemIsSelectable)
                {
                    tree->setCurrentItem(*it);
                    break;
                }
                ++it;
            }
        }

        void MoveSelection(QTreeWidget* tree, int direction)
        {
            auto* current = tree->currentItem();
            QTreeWidgetItemIterator it{tree};
            QTreeWidgetItem* prev = nullptr;
            QTreeWidgetItem* next = nullptr;
            bool foundCurrent = false;
            while (*it)
            {
                auto* node = *it;
                ++it;
                if (!(node->flags() & Qt::ItemIsSelectable))
                    continue;
                if (node == current)
                {
                    foundCurrent = true;
                    continue;
                }
                if (!foundCurrent)
                {
                    prev = node;
                }
                else if (next == nullptr)
                {
                    next = node;
                    break;
                }
            }
            if (direction > 0 && next != nullptr)
                tree->setCurrentItem(next);
            else if (direction < 0 && prev != nullptr)
                tree->setCurrentItem(prev);
        }

        struct ArrowFilter : public QObject
        {
            QTreeWidget* m_tree{nullptr};
            bool eventFilter(QObject*, QEvent* e) override
            {
                if (e->type() != QEvent::KeyPress)
                    return false;
                auto* key = static_cast<QKeyEvent*>(e);
                if (key->key() == Qt::Key_Down)
                {
                    MoveSelection(m_tree, 1);
                    return true;
                }
                if (key->key() == Qt::Key_Up)
                {
                    MoveSelection(m_tree, -1);
                    return true;
                }
                return false;
            }
        };

        void ShowInternal(QWidget* anchor, const QPoint& globalPos,
                          const queen::ComponentRegistry<256>& registry, queen::World& world,
                          IsPresentFn isAlreadyPresent, bool allEntitiesHaveScript,
                          AddComponentPopup::AcceptFn onAccept)
        {
            // Parent to the top-level window: the anchor (an inspector button) can be destroyed
            // synchronously when onAccept triggers an inspector rebuild — anchoring there would
            // tear the popup out from under us mid-signal-dispatch.
            QWidget* popupParent = anchor->window() != nullptr ? anchor->window() : anchor;
            auto* popup = new QFrame{popupParent, Qt::Popup};
            popup->setStyleSheet(kPopupStyle);
            popup->setFixedSize(320, 380);
            popup->setAttribute(Qt::WA_DeleteOnClose);

            auto* layout = new QVBoxLayout{popup};
            layout->setContentsMargins(8, 8, 8, 8);
            layout->setSpacing(4);

            auto* search = new QLineEdit{popup};
            search->setPlaceholderText(QStringLiteral("Search components..."));
            layout->addWidget(search);

            auto* tree = new QTreeWidget{popup};
            tree->setHeaderHidden(true);
            tree->setRootIsDecorated(false);
            tree->setIndentation(12);
            layout->addWidget(tree);

            Populate(tree, registry, world, isAlreadyPresent, allEntitiesHaveScript, QString{});

            auto accept = [popup, tree, onAccept = std::move(onAccept)]() {
                AddComponentChoice choice{};
                bool hasChoice = false;
                auto* item = tree->currentItem();
                if (item != nullptr && (item->flags() & Qt::ItemIsSelectable))
                {
                    const int kind = item->data(0, kItemKindRole).toInt();
                    choice.m_kind = static_cast<AddComponentKind>(kind);
                    if (choice.m_kind == AddComponentKind::COMPONENT)
                    {
                        choice.m_componentTypeId =
                            static_cast<queen::TypeId>(item->data(0, kItemValueRole).toULongLong());
                    }
                    else
                    {
                        choice.m_scriptNameHash = item->data(0, kItemValueRole).toUInt();
                    }
                    hasChoice = true;
                }
                if (hasChoice && onAccept != nullptr)
                    onAccept(choice);
                popup->close();
            };

            QObject::connect(search, &QLineEdit::textChanged, tree,
                             [tree, &registry, &world, isAlreadyPresent,
                              allEntitiesHaveScript](const QString& text) {
                                 Populate(tree, registry, world, isAlreadyPresent, allEntitiesHaveScript, text);
                             });
            QObject::connect(tree, &QTreeWidget::itemActivated, popup,
                             [accept](QTreeWidgetItem* item, int) {
                                 if (item != nullptr && (item->flags() & Qt::ItemIsSelectable))
                                     accept();
                             });
            QObject::connect(tree, &QTreeWidget::itemDoubleClicked, popup,
                             [accept](QTreeWidgetItem* item, int) {
                                 if (item != nullptr && (item->flags() & Qt::ItemIsSelectable))
                                     accept();
                             });
            QObject::connect(search, &QLineEdit::returnPressed, popup, accept);

            auto* filter = new ArrowFilter{};
            filter->setParent(popup);
            filter->m_tree = tree;
            search->installEventFilter(filter);

            popup->move(globalPos);
            popup->show();
            search->setFocus();
        }
    } // namespace

    void AddComponentPopup::Show(QWidget* anchor, const QPoint& globalPos,
                                 const queen::ComponentRegistry<256>& registry, queen::World& world,
                                 queen::Entity entity, AcceptFn onAccept)
    {
        const queen::TypeId scriptTypeId = queen::TypeIdOf<propolis::PropolisScript>();
        const bool hasScript = world.HasComponent(entity, scriptTypeId);
        auto isAlreadyPresent = [&world, entity](queen::TypeId typeId) {
            return world.HasComponent(entity, typeId);
        };
        ShowInternal(anchor, globalPos, registry, world, std::move(isAlreadyPresent), hasScript,
                     std::move(onAccept));
    }

    void AddComponentPopup::ShowForEntities(QWidget* anchor, const QPoint& globalPos,
                                            const queen::ComponentRegistry<256>& registry, queen::World& world,
                                            const wax::Vector<queen::Entity>& entities, AcceptFn onAccept)
    {
        const queen::TypeId scriptTypeId = queen::TypeIdOf<propolis::PropolisScript>();
        bool allHaveScript = !entities.IsEmpty();
        for (size_t i = 0; i < entities.Size(); ++i)
        {
            if (!world.HasComponent(entities[i], scriptTypeId))
            {
                allHaveScript = false;
                break;
            }
        }

        auto isAlreadyPresent = [&world, &entities](queen::TypeId typeId) {
            for (size_t i = 0; i < entities.Size(); ++i)
            {
                if (!world.HasComponent(entities[i], typeId))
                    return false;
            }
            return true;
        };
        ShowInternal(anchor, globalPos, registry, world, std::move(isAlreadyPresent), allHaveScript,
                     std::move(onAccept));
    }
} // namespace forge
