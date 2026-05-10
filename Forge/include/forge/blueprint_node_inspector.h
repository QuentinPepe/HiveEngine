#pragma once

#include <propolis/graph/graph.h>

#include <QWidget>

#include <cstddef>

namespace queen
{
    template <size_t> class ComponentRegistry;
}

namespace forge
{
    class BlueprintNode;
    class BlueprintEditor;

    class BlueprintNodeInspector : public QWidget
    {
        Q_OBJECT

    public:
        explicit BlueprintNodeInspector(QWidget* parent = nullptr);

        void SetEditor(BlueprintEditor* editor);
        void SetComponentRegistry(const queen::ComponentRegistry<256>* registry);

        void InspectNode(BlueprintNode* node);
        void InspectVariable(uint32_t variableId);
        void ShowGraphProperties();
        void Clear();

    signals:
        void graphModified();

    private:
        void BuildNodeUI(BlueprintNode* node);
        void BuildComponentPicker(QLayout* parentLayout, QWidget* parentWidget,
                                  BlueprintNode* node, propolis::Node* graphNode);
        void BuildDefaultValues(QLayout* parentLayout, QWidget* parentWidget,
                                BlueprintNode* node);
        void BuildVariableUI(propolis::Variable* var);
        void BuildGraphUI();
        void ReplaceContent(QWidget* newContent);

        BlueprintEditor* m_editor{};
        QWidget* m_content{};
        const queen::ComponentRegistry<256>* m_componentRegistry{};
    };
} // namespace forge
