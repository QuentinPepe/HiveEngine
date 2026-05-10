#include <forge/blueprint_outline_panel.h>
#include <forge/blueprint_editor.h>
#include <forge/blueprint_mime.h>
#include <forge/blueprint_pin.h>

#include <propolis/types/ptype.h>

#include <QDrag>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QInputDialog>

namespace forge
{
    class VariableDragTree : public QTreeWidget
    {
    public:
        using QTreeWidget::QTreeWidget;

    protected:
        void startDrag(Qt::DropActions) override
        {
            auto* item = currentItem();
            if (!item || !item->data(0, Qt::UserRole).isValid())
            {
                return;
            }

            auto* mime = new QMimeData{};
            mime->setData(kVariableDragMime, QByteArray::number(item->data(0, Qt::UserRole).toUInt()));

            auto* drag = new QDrag{this};
            drag->setMimeData(mime);
            drag->exec(Qt::CopyAction);
        }
    };

    BlueprintOutlinePanel::BlueprintOutlinePanel(QWidget* parent)
        : QWidget{parent}
    {
        auto* layout = new QVBoxLayout{this};
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto* header = new QWidget{this};
        header->setFixedHeight(32);
        header->setStyleSheet("background: #141414; border-bottom: 1px solid #2a2a2a;");
        auto* headerLayout = new QHBoxLayout{header};
        headerLayout->setContentsMargins(8, 2, 8, 2);
        headerLayout->setSpacing(4);

        auto* title = new QLabel{"Explorer", header};
        title->setStyleSheet("color: #f0a500; font-weight: bold; font-size: 11px; border: none;");

        auto* addBtn = new QPushButton{"+", header};
        addBtn->setFixedSize(22, 22);
        addBtn->setToolTip("Add Variable");
        addBtn->setStyleSheet(
            "QPushButton { background: #1a1a1a; color: #ccc; border: 1px solid #2a2a2a;"
            "  border-radius: 3px; font-size: 14px; font-weight: bold; padding: 0; }"
            "QPushButton:hover { border-color: #f0a500; color: #f0a500; }");

        headerLayout->addWidget(title);
        headerLayout->addStretch();
        headerLayout->addWidget(addBtn);
        layout->addWidget(header);

        connect(addBtn, &QPushButton::clicked, this, &BlueprintOutlinePanel::OnAddVariable);

        m_searchBar = new QLineEdit{this};
        m_searchBar->setPlaceholderText("Search...");
        m_searchBar->setFixedHeight(24);
        m_searchBar->setStyleSheet(
            "QLineEdit { background: #1a1a1a; color: #e8e8e8; border: 1px solid #2a2a2a;"
            "  border-radius: 3px; padding: 2px 6px; font-size: 10px; margin: 4px; }"
            "QLineEdit:focus { border-color: #f0a500; }");
        layout->addWidget(m_searchBar);

        connect(m_searchBar, &QLineEdit::textChanged, this, [this](const QString&) { RebuildTree(); });

        m_tree = new VariableDragTree{this};
        m_tree->setHeaderHidden(true);
        m_tree->setColumnCount(2);
        m_tree->setIndentation(16);
        m_tree->setRootIsDecorated(true);
        m_tree->setAnimated(true);
        m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
        m_tree->setDragEnabled(true);
        m_tree->setDragDropMode(QAbstractItemView::DragOnly);
        m_tree->setStyleSheet(
            "QTreeWidget { background: #111; border: none; outline: none; }"
            "QTreeWidget::item { padding: 3px 0px; }"
            "QTreeWidget::item:hover { background: #1e1e1e; }"
            "QTreeWidget::item:selected { background: #3d2e0a; color: #f0a500; }");

        layout->addWidget(m_tree);

        connect(m_tree, &QTreeWidget::itemClicked, this, &BlueprintOutlinePanel::OnTreeItemClicked);
        connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &BlueprintOutlinePanel::OnTreeContextMenu);

        m_tree->header()->setStretchLastSection(false);
        m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    }

    void BlueprintOutlinePanel::SetEditor(BlueprintEditor* editor)
    {
        m_editor = editor;
        Refresh();
    }

    void BlueprintOutlinePanel::Refresh()
    {
        RebuildTree();
    }

    void BlueprintOutlinePanel::RebuildTree()
    {
        m_tree->clear();
        m_variablesRoot = nullptr;
        m_eventsRoot = nullptr;

        if (!m_editor)
        {
            return;
        }

        const auto& graph = m_editor->GetGraph();
        QString filter = m_searchBar->text().toLower();

        m_variablesRoot = new QTreeWidgetItem{m_tree, {"Variables"}};
        m_variablesRoot->setFlags(Qt::ItemIsEnabled);
        m_variablesRoot->setExpanded(true);
        QFont boldFont = m_variablesRoot->font(0);
        boldFont.setBold(true);
        boldFont.setPointSize(9);
        m_variablesRoot->setFont(0, boldFont);
        m_variablesRoot->setForeground(0, QColor{0xf0, 0xa5, 0x00});

        for (size_t i = 0; i < graph.Variables().Size(); ++i)
        {
            const auto& var = graph.Variables()[i];
            QString name = QString::fromUtf8(var.m_name.CStr());

            if (!filter.isEmpty() && !name.toLower().contains(filter))
            {
                continue;
            }

            QString typeName = QString::fromUtf8(propolis::PTypeKindName(var.m_type.m_kind));
            QColor typeColor = PTypeColor(var.m_type);

            auto* item = new QTreeWidgetItem{m_variablesRoot, {name, typeName}};
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
            item->setData(0, Qt::UserRole, var.m_id.m_value);
            item->setForeground(0, QColor{0xe8, 0xe8, 0xe8});
            item->setForeground(1, typeColor);

            QFont typeFont = item->font(1);
            typeFont.setPointSize(8);
            item->setFont(1, typeFont);

            if (var.m_exposed)
            {
                item->setIcon(0, QIcon{});
                item->setText(0, name + " *");
            }
        }

        m_eventsRoot = new QTreeWidgetItem{m_tree, {"Events"}};
        m_eventsRoot->setFlags(Qt::ItemIsEnabled);
        m_eventsRoot->setExpanded(true);
        m_eventsRoot->setFont(0, boldFont);
        m_eventsRoot->setForeground(0, QColor{0xf0, 0xa5, 0x00});

        if (graph.Mode() == propolis::GraphMode::ENTITY_SCRIPT)
        {
            auto addEvent = [&](const char* name) {
                if (!filter.isEmpty() && !QString{name}.toLower().contains(filter))
                {
                    return;
                }
                auto* item = new QTreeWidgetItem{m_eventsRoot, {name, "Event"}};
                item->setFlags(Qt::ItemIsEnabled);
                item->setForeground(0, QColor{0xe8, 0xe8, 0xe8});
                item->setForeground(1, QColor{0xa8, 0x2a, 0x2a});
                QFont f = item->font(1);
                f.setPointSize(8);
                item->setFont(1, f);
            };
            addEvent("OnAttach");
            addEvent("OnTick");
            addEvent("OnDetach");
        }
    }

    void BlueprintOutlinePanel::OnAddVariable()
    {
        if (!m_editor)
        {
            return;
        }

        bool ok = false;
        QString name = QInputDialog::getText(this, "New Variable", "Name:", QLineEdit::Normal, "NewVar", &ok);
        if (!ok || name.isEmpty())
        {
            return;
        }

        m_editor->SnapshotBeforeAction();
        (void)m_editor->GetGraph().AddVariable(name.toUtf8().constData(), propolis::PType::Float32());
        m_editor->PushUndoAfterAction();
        Refresh();
    }

    void BlueprintOutlinePanel::OnTreeItemClicked(QTreeWidgetItem* item, int)
    {
        if (!item || !item->parent())
        {
            emit nothingSelected();
            return;
        }

        if (item->parent() == m_variablesRoot)
        {
            uint32_t varId = item->data(0, Qt::UserRole).toUInt();
            emit variableSelected(varId);
        }
    }

    void BlueprintOutlinePanel::OnTreeContextMenu(const QPoint& pos)
    {
        auto* item = m_tree->itemAt(pos);
        if (!item || !item->parent() || item->parent() != m_variablesRoot)
        {
            return;
        }

        uint32_t varId = item->data(0, Qt::UserRole).toUInt();

        QMenu menu{this};
        menu.setStyleSheet(
            "QMenu { background: #1a1a1a; border: 1px solid #333; border-radius: 4px; padding: 4px; }"
            "QMenu::item { padding: 6px 24px; color: #e8e8e8; border-radius: 2px; }"
            "QMenu::item:selected { background: #3d2e0a; color: #f0a500; }");

        auto* renameAction = menu.addAction("Rename");
        auto* deleteAction = menu.addAction("Delete");
        menu.addSeparator();
        auto* toggleExposed = menu.addAction("Toggle Exposed");

        QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
        if (!chosen || !m_editor)
        {
            return;
        }

        propolis::VariableId vid{varId};

        if (chosen == deleteAction)
        {
            m_editor->SnapshotBeforeAction();
            m_editor->GetGraph().RemoveVariable(vid);
            m_editor->PushUndoAfterAction();
            Refresh();
        }
        else if (chosen == renameAction)
        {
            auto* var = m_editor->GetGraph().FindVariable(vid);
            if (!var)
            {
                return;
            }
            bool ok = false;
            QString newName = QInputDialog::getText(this, "Rename Variable", "Name:",
                                                     QLineEdit::Normal,
                                                     QString::fromUtf8(var->m_name.CStr()), &ok);
            if (ok && !newName.isEmpty())
            {
                m_editor->SnapshotBeforeAction();
                var->m_name = newName.toUtf8().constData();
                m_editor->PushUndoAfterAction();
                Refresh();
            }
        }
        else if (chosen == toggleExposed)
        {
            auto* var = m_editor->GetGraph().FindVariable(vid);
            if (var)
            {
                m_editor->SnapshotBeforeAction();
                var->m_exposed = !var->m_exposed;
                m_editor->PushUndoAfterAction();
                Refresh();
            }
        }
    }
} // namespace forge
