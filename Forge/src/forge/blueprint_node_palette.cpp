#include <forge/blueprint_node_palette.h>

#include <propolis/nodes/node_descriptor.h>
#include <propolis/runtime/function_registry.h>

#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMap>
#include <QObject>
#include <QString>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace forge
{
    static constexpr const char* kPaletteStyle = R"(
        QFrame { background: #1a1a1a; border: 1px solid #333; border-radius: 6px; }
        QLineEdit { background: #0d0d0d; color: #e8e8e8; border: 1px solid #444; border-radius: 3px;
                     padding: 6px 8px; font-size: 12px; selection-background-color: #3d2e0a; }
        QTreeWidget { background: transparent; border: none; color: #e8e8e8; font-size: 11px; outline: none; }
        QTreeWidget::item { padding: 3px 8px; border-radius: 2px; }
        QTreeWidget::item:selected { background: #3d2e0a; color: #f0a500; }
        QTreeWidget::item:hover:!selected { background: #252525; }
        QTreeWidget::branch { background: transparent; }
        QTreeWidget::branch:has-children:closed { image: url(none); border-image: none;
            border-left: 6px solid #666; border-top: 4px solid transparent; border-bottom: 4px solid transparent; }
        QTreeWidget::branch:has-children:open { image: url(none); border-image: none;
            border-top: 6px solid #666; border-left: 4px solid transparent; border-right: 4px solid transparent; }
        QHeaderView { background: transparent; }
        QHeaderView::section { background: transparent; border: none; color: #888; font-size: 10px; padding: 2px 8px; }
    )";

    // UserRole stores the entry pointer (NodeDescriptor* or FunctionEntry*).
    // UserRole+1 stores the kind (0 = builtin, 1 = user function).
    static constexpr int kKindRole = Qt::UserRole + 1;

    static void PopulatePaletteTree(QTreeWidget* tree, const propolis::NodeRegistry& registry,
                                     const propolis::FunctionRegistry* functions,
                                     const QString& filter)
    {
        tree->clear();
        QString lower = filter.toLower();
        bool filtering = !lower.isEmpty();

        QMap<QString, QTreeWidgetItem*> catItems;

        auto getOrMakeCategory = [&](const QString& cat) -> QTreeWidgetItem* {
            auto it = catItems.find(cat);
            if (it != catItems.end())
            {
                return it.value();
            }
            auto* catItem = new QTreeWidgetItem{tree, {cat}};
            catItem->setFlags(Qt::ItemIsEnabled);
            QFont f = catItem->font(0);
            f.setBold(true);
            catItem->setFont(0, f);
            catItem->setForeground(0, QColor{0xf0, 0xa5, 0x00});
            catItems[cat] = catItem;
            return catItem;
        };

        for (size_t i = 0; i < registry.All().Size(); ++i)
        {
            const auto& desc = registry.All()[i];
            QString name = QString::fromUtf8(desc.m_name.CStr());
            QString cat = QString::fromUtf8(desc.m_category.CStr());

            if (filtering && !(name.toLower().contains(lower) || cat.toLower().contains(lower)))
            {
                continue;
            }

            auto* catItem = getOrMakeCategory(cat);
            auto* nodeItem = new QTreeWidgetItem{catItem, {name}};
            nodeItem->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<const void*>(&desc)));
            nodeItem->setData(0, kKindRole, 0);
        }

        if (functions)
        {
            const auto& all = functions->All();
            for (size_t i = 0; i < all.Size(); ++i)
            {
                const auto& fn = all[i];
                QString name = QString::fromUtf8(fn.m_name);
                QString cat = QString::fromUtf8(fn.m_category && fn.m_category[0] ? fn.m_category : "User");

                if (filtering && !(name.toLower().contains(lower) || cat.toLower().contains(lower)))
                {
                    continue;
                }

                auto* catItem = getOrMakeCategory(cat);
                auto* nodeItem = new QTreeWidgetItem{catItem, {name}};
                nodeItem->setData(0, Qt::UserRole, QVariant::fromValue(static_cast<const void*>(&fn)));
                nodeItem->setData(0, kKindRole, 1);
            }
        }

        tree->expandAll();

        QTreeWidgetItemIterator it{tree};
        while (*it)
        {
            if ((*it)->data(0, Qt::UserRole).isValid())
            {
                tree->setCurrentItem(*it);
                break;
            }
            ++it;
        }
    }

    static void MovePaletteSelection(QTreeWidget* tree, int direction)
    {
        auto* current = tree->currentItem();
        QTreeWidgetItemIterator it{tree};
        QTreeWidgetItem* prev = nullptr;
        QTreeWidgetItem* next = nullptr;
        bool found = false;

        while (*it)
        {
            if (*it == current)
            {
                found = true;
            }
            else if ((*it)->data(0, Qt::UserRole).isValid())
            {
                if (!found)
                {
                    prev = *it;
                }
                else if (!next)
                {
                    next = *it;
                    break;
                }
            }
            ++it;
        }

        if (direction > 0 && next)
        {
            tree->setCurrentItem(next);
        }
        else if (direction < 0 && prev)
        {
            tree->setCurrentItem(prev);
        }
    }

    namespace
    {
        struct PaletteArrowFilter : public QObject
        {
            QTreeWidget* m_tree{};
            bool eventFilter(QObject*, QEvent* e) override
            {
                if (e->type() == QEvent::KeyPress)
                {
                    auto* ke = static_cast<QKeyEvent*>(e);
                    if (ke->key() == Qt::Key_Down)
                    {
                        MovePaletteSelection(m_tree, 1);
                        return true;
                    }
                    if (ke->key() == Qt::Key_Up)
                    {
                        MovePaletteSelection(m_tree, -1);
                        return true;
                    }
                }
                return false;
            }
        };
    } // namespace

    void BlueprintNodePalette::Show(QWidget* anchor, const QPoint& globalPos,
                                     const propolis::NodeRegistry& registry,
                                     const propolis::FunctionRegistry* functions,
                                     AcceptFn onAccept)
    {
        auto* popup = new QFrame{anchor, Qt::Popup};
        popup->setStyleSheet(kPaletteStyle);
        popup->setFixedSize(320, 420);
        popup->setAttribute(Qt::WA_DeleteOnClose);

        auto* layout = new QVBoxLayout{popup};
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(4);

        auto* search = new QLineEdit{popup};
        search->setPlaceholderText("Search nodes...");
        layout->addWidget(search);

        auto* tree = new QTreeWidget{popup};
        tree->setHeaderHidden(true);
        tree->setRootIsDecorated(true);
        tree->setIndentation(16);
        tree->setAnimated(true);
        layout->addWidget(tree);

        PopulatePaletteTree(tree, registry, functions, "");

        auto accept = [popup, search, tree, onAccept]() {
            search->blockSignals(true);
            auto* item = tree->currentItem();
            if (item && item->data(0, Qt::UserRole).isValid())
            {
                int kind = item->data(0, kKindRole).toInt();
                const void* ptr = item->data(0, Qt::UserRole).value<const void*>();
                PaletteSelection sel;
                if (kind == 1)
                {
                    sel.m_function = static_cast<const propolis::FunctionEntry*>(ptr);
                }
                else
                {
                    sel.m_descriptor = static_cast<const propolis::NodeDescriptor*>(ptr);
                }
                if ((sel.m_descriptor || sel.m_function) && onAccept)
                {
                    onAccept(sel);
                }
            }
            popup->close();
        };

        QObject::connect(search, &QLineEdit::textChanged, tree, [tree, &registry, functions](const QString& f) {
            PopulatePaletteTree(tree, registry, functions, f);
        });
        QObject::connect(tree, &QTreeWidget::itemDoubleClicked, popup, [accept](QTreeWidgetItem* item) {
            if (item && item->data(0, Qt::UserRole).isValid())
            {
                accept();
            }
        });
        QObject::connect(search, &QLineEdit::returnPressed, popup, accept);

        auto* filter = new PaletteArrowFilter{};
        filter->setParent(popup);
        filter->m_tree = tree;
        search->installEventFilter(filter);

        popup->move(globalPos);
        popup->show();
        search->setFocus();
    }
} // namespace forge
