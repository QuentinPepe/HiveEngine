#pragma once

#include <propolis/graph/graph.h>
#include <propolis/nodes/node_descriptor.h>
#include <propolis/compiler/graph_validator.h>
#include <propolis/runtime/function_registry.h>

#include <QGraphicsView>

#include <cstddef>
#include <functional>

namespace queen
{
    template <size_t> class ComponentRegistry;
}

#include <unordered_map>

namespace forge
{
    class BlueprintNode;
    class BlueprintPin;
    class BlueprintConnection;
    class BlueprintDragConnection;
    class EditorUndoManager;

    class BlueprintEditor : public QGraphicsView
    {
        Q_OBJECT

    public:
        explicit BlueprintEditor(QWidget* parent = nullptr);
        ~BlueprintEditor() override;

        BlueprintNode* AddNodeFromRegistry(const char* descriptorName, const QPointF& pos);
        BlueprintNode* AddUserFunctionNode(const propolis::FunctionEntry& fn, const QPointF& pos);
        void RemoveNode(BlueprintNode* node);
        void RemoveConnection(BlueprintConnection* conn);
        void DeleteSelected();

        propolis::Graph& GetGraph() { return m_graph; }
        const propolis::Graph& GetGraph() const { return m_graph; }
        const propolis::NodeRegistry& GetRegistry() const { return m_registry; }

        // Resolved on every palette open, so hot-reload / late gameplay-DLL load picks up
        // user functions without re-creating the panel.
        using FunctionRegistryProvider = std::function<const propolis::FunctionRegistry*()>;
        void SetFunctionRegistryProvider(FunctionRegistryProvider provider)
        {
            m_functionRegistryProvider = static_cast<FunctionRegistryProvider&&>(provider);
        }

        void RebuildComponentPins(propolis::Node* graphNode,
                                  const queen::ComponentRegistry<256>* registry);
        void RebuildBreakPins(propolis::Node* graphNode);
        void RebuildMakePins(propolis::Node* graphNode);
        BlueprintNode* NodeVisual(propolis::NodeId id) const;

        void RebuildVisualsFromGraph();
        void SetUndoManager(EditorUndoManager* undo) { m_undo = undo; }
        void Undo();
        void Redo();
        void SnapshotBeforeAction();
        void PushUndoAfterAction();

    signals:
        void graphModified();
        void nodeSelected(BlueprintNode* node);
        void selectionCleared();

    protected:
        void drawBackground(QPainter* painter, const QRectF& rect) override;
        void wheelEvent(QWheelEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;
        void keyPressEvent(QKeyEvent* event) override;
        void contextMenuEvent(QContextMenuEvent* event) override;
        void dragEnterEvent(QDragEnterEvent* event) override;
        void dragMoveEvent(QDragMoveEvent* event) override;
        void dropEvent(QDropEvent* event) override;

    private:
        void ShowNodePalette(const QPoint& globalPos, const QPointF& scenePos);
        BlueprintPin* PinAt(const QPointF& scenePos) const;
        bool CanConnect(BlueprintPin* a, BlueprintPin* b) const;
        void FinalizeDrag(const QPointF& scenePos);

        BlueprintNode* CreateNodeVisual(propolis::NodeId nodeId, const propolis::NodeDescriptor& desc,
                                        const QPointF& pos);
        BlueprintNode* CreateUserFunctionVisual(propolis::NodeId nodeId, const QString& title,
                                                 const QString& category, const QPointF& pos);
        BlueprintConnection* ConnectPinsInternal(BlueprintPin* source, BlueprintPin* target);

        void RunValidation();

        BlueprintPin* FindPinVisual(propolis::PinId id) const;
        BlueprintNode* FindNodeVisual(propolis::NodeId id) const;
        BlueprintConnection* FindConnectionVisual(propolis::EdgeId id) const;

        QGraphicsScene* m_scene;
        propolis::Graph m_graph;
        propolis::NodeRegistry m_registry;
        FunctionRegistryProvider m_functionRegistryProvider;
        propolis::GraphValidator m_validator;
        propolis::ValidationResult m_lastValidation;

        std::unordered_map<uint32_t, BlueprintPin*> m_pinMap;
        std::unordered_map<uint32_t, BlueprintNode*> m_nodeMap;
        std::unordered_map<uint32_t, BlueprintConnection*> m_edgeMap;

        EditorUndoManager* m_undo{};
        wax::String m_snapshotBefore;

        BlueprintDragConnection* m_dragConn{};
        bool m_destroying{false};
        bool m_isPanning{false};
        QPoint m_lastPanPos;
        qreal m_zoom{1.0};

        static constexpr qreal MIN_ZOOM = 0.1;
        static constexpr qreal MAX_ZOOM = 3.0;
        static constexpr qreal GRID_SPACING = 20.0;
        static constexpr qreal MAJOR_GRID_EVERY = 5.0;
    };
} // namespace forge
