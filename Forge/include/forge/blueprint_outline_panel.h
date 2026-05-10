#pragma once

#include <propolis/graph/graph.h>

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;

namespace forge
{
    class BlueprintEditor;

    class BlueprintOutlinePanel : public QWidget
    {
        Q_OBJECT

    public:
        explicit BlueprintOutlinePanel(QWidget* parent = nullptr);

        void SetEditor(BlueprintEditor* editor);
        void Refresh();

    signals:
        void variableSelected(uint32_t variableId);
        void nothingSelected();

    private:
        void OnAddVariable();
        void OnTreeItemClicked(QTreeWidgetItem* item, int column);
        void OnTreeContextMenu(const QPoint& pos);
        void RebuildTree();

        BlueprintEditor* m_editor{};
        QLineEdit* m_searchBar{};
        QTreeWidget* m_tree{};
        QTreeWidgetItem* m_variablesRoot{};
        QTreeWidgetItem* m_eventsRoot{};
    };
} // namespace forge
