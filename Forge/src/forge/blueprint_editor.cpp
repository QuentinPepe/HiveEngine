#include <forge/blueprint_editor.h>
#include <forge/blueprint_connection.h>
#include <forge/blueprint_mime.h>
#include <forge/blueprint_node.h>
#include <forge/blueprint_node_palette.h>
#include <forge/blueprint_pin.h>
#include <forge/editor_undo.h>
#include <forge/theme.h>

#include <hive/math/types.h>

#include <queen/core/type_id.h>
#include <queen/reflect/component_registry.h>

#include <QGraphicsScene>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>

#include <cmath>
#include <cstring>

namespace forge
{
    static QColor CategoryColor(const char* category)
    {
        if (std::strcmp(category, "Math") == 0)       return {0x4a, 0x7a, 0x4a};
        if (std::strcmp(category, "Logic") == 0)      return {0x2a, 0x6e, 0xa8};
        if (std::strcmp(category, "Conversion") == 0) return {0x6a, 0x4a, 0x7a};
        if (std::strcmp(category, "Event") == 0)      return {0xa8, 0x2a, 0x2a};
        if (std::strcmp(category, "Flow") == 0)       return {0x88, 0x88, 0x88};
        if (std::strcmp(category, "Variable") == 0)   return {0xa8, 0x6e, 0x2a};
        if (std::strcmp(category, "Component") == 0)  return {0x2a, 0x9a, 0xa8};
        if (std::strcmp(category, "Utility") == 0)    return {0x70, 0x70, 0x70};
        if (std::strcmp(category, "Debug") == 0)      return {0xa8, 0x6e, 0xa8};
        return {0x55, 0x55, 0x55};
    }

    BlueprintEditor::BlueprintEditor(QWidget* parent)
        : QGraphicsView{parent}
    {
        m_scene = new QGraphicsScene{this};
        m_scene->setSceneRect(-10000, -10000, 20000, 20000);
        setScene(m_scene);

        setRenderHint(QPainter::Antialiasing);
        setRenderHint(QPainter::SmoothPixmapTransform);
        setViewportUpdateMode(FullViewportUpdate);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setTransformationAnchor(NoAnchor);
        setDragMode(RubberBandDrag);
        setBackgroundBrush(theme::kBackground);

        setAcceptDrops(true);
        setStyleSheet("QGraphicsView { border: none; }");

        connect(m_scene, &QGraphicsScene::selectionChanged, this, [this]() {
            if (m_destroying)
            {
                return;
            }
            auto selected = m_scene->selectedItems();
            BlueprintNode* node = nullptr;
            for (auto* item : selected)
            {
                node = dynamic_cast<BlueprintNode*>(item);
                if (node)
                {
                    break;
                }
            }
            if (node)
            {
                emit nodeSelected(node);
            }
            else
            {
                emit selectionCleared();
            }
        });
    }

    BlueprintEditor::~BlueprintEditor()
    {
        m_destroying = true;
        if (m_undo)
        {
            m_undo->Clear();
        }
        for (auto& [_, conn] : m_edgeMap)
        {
            conn->Detach();
        }
    }

    void BlueprintEditor::SnapshotBeforeAction()
    {
        m_snapshotBefore = m_graph.SerializeToJson();
    }

    void BlueprintEditor::PushUndoAfterAction()
    {
        if (!m_undo || m_snapshotBefore.Size() == 0)
        {
            return;
        }

        wax::String after = m_graph.SerializeToJson();
        wax::String before = static_cast<wax::String&&>(m_snapshotBefore);

        m_undo->Push(
            [this, before]() {
                (void)m_graph.DeserializeFromJson(before.CStr());
                RebuildVisualsFromGraph();
                emit graphModified();
            },
            [this, after]() {
                (void)m_graph.DeserializeFromJson(after.CStr());
                RebuildVisualsFromGraph();
                emit graphModified();
            });

        emit graphModified();
    }

    void BlueprintEditor::Undo()
    {
        if (m_undo)
        {
            m_undo->Undo();
        }
    }

    void BlueprintEditor::Redo()
    {
        if (m_undo)
        {
            m_undo->Redo();
        }
    }

    BlueprintNode* BlueprintEditor::CreateNodeVisual(propolis::NodeId nodeId,
                                                      const propolis::NodeDescriptor& desc,
                                                      const QPointF& pos)
    {
        QColor color = CategoryColor(desc.m_category.CStr());
        auto* node = new BlueprintNode{nodeId, QString::fromUtf8(desc.m_name.CStr()), color};
        node->setPos(pos);
        m_scene->addItem(node);
        m_nodeMap[nodeId.m_value] = node;

        const propolis::Node* graphNode = m_graph.FindNode(nodeId);
        if (!graphNode)
        {
            return node;
        }

        auto createPinVisuals = [&](const wax::Vector<propolis::PinId>& pinIds) {
            for (size_t i = 0; i < pinIds.Size(); ++i)
            {
                const propolis::Pin* graphPin = m_graph.FindPin(pinIds[i]);
                if (!graphPin)
                {
                    continue;
                }

                auto* pin = new BlueprintPin{
                    graphPin->m_id,
                    graphPin->m_direction,
                    graphPin->m_type,
                    graphPin->m_isExec,
                    QString::fromUtf8(graphPin->m_name.CStr()),
                    node};

                node->AddPin(pin);
                m_pinMap[graphPin->m_id.m_value] = pin;
            }
        };

        createPinVisuals(graphNode->m_inputs);
        createPinVisuals(graphNode->m_outputs);

        return node;
    }

    BlueprintNode* BlueprintEditor::AddNodeFromRegistry(const char* descriptorName, const QPointF& pos)
    {
        const propolis::NodeDescriptor* desc = m_registry.Find(descriptorName);
        if (!desc)
        {
            return nullptr;
        }

        propolis::NodeId nodeId = m_graph.AddNode(descriptorName, desc->m_category.CStr());

        for (size_t i = 0; i < desc->m_pins.Size(); ++i)
        {
            const propolis::PinDescriptor& pd = desc->m_pins[i];
            propolis::PinId pinId = m_graph.AddPin(nodeId, pd.m_direction, pd.m_type, pd.m_name.CStr());

            propolis::Pin* pin = m_graph.FindPin(pinId);
            if (pin)
            {
                pin->m_isExec = (pd.m_type.m_kind == propolis::PTypeKind::SIGNAL);
            }
        }

        propolis::Node* graphNode = m_graph.FindNode(nodeId);
        if (graphNode)
        {
            graphNode->m_posX = static_cast<float>(pos.x());
            graphNode->m_posY = static_cast<float>(pos.y());
        }

        auto* visual = CreateNodeVisual(nodeId, *desc, pos);
        return visual;
    }

    BlueprintNode* BlueprintEditor::CreateUserFunctionVisual(propolis::NodeId nodeId, const QString& title,
                                                              const QString& category, const QPointF& pos)
    {
        QColor color = CategoryColor(category.toUtf8().constData());
        auto* node = new BlueprintNode{nodeId, title, color};
        node->setPos(pos);
        m_scene->addItem(node);
        m_nodeMap[nodeId.m_value] = node;

        const propolis::Node* graphNode = m_graph.FindNode(nodeId);
        if (!graphNode)
        {
            return node;
        }

        auto createPinVisuals = [&](const wax::Vector<propolis::PinId>& pinIds) {
            for (size_t i = 0; i < pinIds.Size(); ++i)
            {
                const propolis::Pin* graphPin = m_graph.FindPin(pinIds[i]);
                if (!graphPin)
                {
                    continue;
                }
                auto* pin = new BlueprintPin{
                    graphPin->m_id,
                    graphPin->m_direction,
                    graphPin->m_type,
                    graphPin->m_isExec,
                    QString::fromUtf8(graphPin->m_name.CStr()),
                    node};
                node->AddPin(pin);
                m_pinMap[graphPin->m_id.m_value] = pin;
            }
        };

        createPinVisuals(graphNode->m_inputs);
        createPinVisuals(graphNode->m_outputs);
        return node;
    }

    BlueprintNode* BlueprintEditor::AddUserFunctionNode(const propolis::FunctionEntry& fn, const QPointF& pos)
    {
        const char* categoryStr = (fn.m_category && fn.m_category[0]) ? fn.m_category : "User";
        propolis::NodeId nodeId = m_graph.AddNode(fn.m_name, categoryStr);

        const bool isVoid = (fn.m_returnType.m_kind == propolis::PTypeKind::SIGNAL);

        if (isVoid)
        {
            propolis::PinId execIn = m_graph.AddPin(nodeId, propolis::PinDirection::INPUT,
                                                     propolis::PType::Signal(), "Exec");
            if (auto* p = m_graph.FindPin(execIn)) p->m_isExec = true;
        }

        for (size_t i = 0; i < fn.m_paramCount; ++i)
        {
            const propolis::ParamInfo& p = fn.m_params[i];
            (void)m_graph.AddPin(nodeId, propolis::PinDirection::INPUT, p.m_type, p.m_name);
        }

        if (isVoid)
        {
            propolis::PinId execOut = m_graph.AddPin(nodeId, propolis::PinDirection::OUTPUT,
                                                      propolis::PType::Signal(), "Exec");
            if (auto* p = m_graph.FindPin(execOut)) p->m_isExec = true;
        }
        else
        {
            (void)m_graph.AddPin(nodeId, propolis::PinDirection::OUTPUT, fn.m_returnType, "Result");
        }

        propolis::Node* graphNode = m_graph.FindNode(nodeId);
        if (graphNode)
        {
            graphNode->m_posX = static_cast<float>(pos.x());
            graphNode->m_posY = static_cast<float>(pos.y());

            graphNode->m_userFn.m_qualifiedCppName = fn.m_qualifiedCppName;
            graphNode->m_userFn.m_returnType = fn.m_returnType;
            for (size_t i = 0; i < fn.m_paramCount; ++i)
            {
                graphNode->m_userFn.m_paramNames.PushBack(wax::String{fn.m_params[i].m_name});
                graphNode->m_userFn.m_paramTypes.PushBack(fn.m_params[i].m_type);
            }
        }

        return CreateUserFunctionVisual(nodeId, QString::fromUtf8(fn.m_name),
                                         QString::fromUtf8(categoryStr), pos);
    }

    BlueprintConnection* BlueprintEditor::ConnectPinsInternal(BlueprintPin* source, BlueprintPin* target)
    {
        SnapshotBeforeAction();

        propolis::EdgeId edgeId = m_graph.AddEdge(source->PropolisId(), target->PropolisId());
        if (!edgeId)
        {
            return nullptr;
        }

        auto* conn = new BlueprintConnection{edgeId, source, target};
        m_scene->addItem(conn);
        m_edgeMap[edgeId.m_value] = conn;
        source->update();
        target->update();

        bool needsRebuild = false;

        propolis::Node* targetNode = m_graph.FindNode(target->Node()->PropolisId());
        if (targetNode)
        {
            const propolis::NodeDescriptor* desc = m_registry.Find(targetNode->m_title.CStr());
            if (desc && propolis::HasFlag(desc->m_flags, propolis::NodeFlag::STRUCT_BREAK))
            {
                RebuildBreakPins(targetNode);
                needsRebuild = true;
            }
        }

        propolis::Node* sourceNode = m_graph.FindNode(source->Node()->PropolisId());
        if (sourceNode)
        {
            const propolis::NodeDescriptor* desc = m_registry.Find(sourceNode->m_title.CStr());
            if (desc && propolis::HasFlag(desc->m_flags, propolis::NodeFlag::STRUCT_MAKE))
            {
                RebuildMakePins(sourceNode);
                needsRebuild = true;
            }
        }

        if (needsRebuild)
        {
            QTimer::singleShot(0, this, [this]() { RebuildVisualsFromGraph(); });
        }

        PushUndoAfterAction();
        return conn;
    }

    void BlueprintEditor::RemoveConnection(BlueprintConnection* conn)
    {
        if (!conn)
        {
            return;
        }
        m_graph.RemoveEdge(conn->PropolisId());
        m_edgeMap.erase(conn->PropolisId().m_value);
        m_scene->removeItem(conn);
        delete conn;
    }

    void BlueprintEditor::RemoveNode(BlueprintNode* node)
    {
        if (!node)
        {
            return;
        }

        auto detachPins = [this](const std::vector<BlueprintPin*>& pins) {
            for (auto* pin : pins)
            {
                auto conns = pin->Connections();
                for (auto* conn : conns)
                {
                    m_edgeMap.erase(conn->PropolisId().m_value);
                    m_scene->removeItem(conn);
                    delete conn;
                }
                m_pinMap.erase(pin->PropolisId().m_value);
            }
        };
        detachPins(node->Inputs());
        detachPins(node->Outputs());

        m_graph.RemoveNode(node->PropolisId());
        m_nodeMap.erase(node->PropolisId().m_value);
        m_scene->removeItem(node);
        delete node;
    }

    void BlueprintEditor::DeleteSelected()
    {
        SnapshotBeforeAction();

        auto selected = m_scene->selectedItems();

        for (auto* item : selected)
        {
            if (auto* conn = dynamic_cast<BlueprintConnection*>(item))
            {
                RemoveConnection(conn);
            }
        }

        selected = m_scene->selectedItems();
        for (auto* item : selected)
        {
            if (auto* node = dynamic_cast<BlueprintNode*>(item))
            {
                RemoveNode(node);
            }
        }
        RunValidation();
        PushUndoAfterAction();
    }

    void BlueprintEditor::RunValidation()
    {
        m_lastValidation = m_validator.Validate(m_graph, m_registry);

        for (auto& [id, pin] : m_pinMap)
        {
            const propolis::PType* t = m_lastValidation.m_resolvedPinTypes.Find(id);
            if (t)
            {
                pin->SetResolvedType(*t);
            }

            bool hasError = false;
            for (size_t i = 0; i < m_lastValidation.m_errors.Size(); ++i)
            {
                if (m_lastValidation.m_errors[i].m_pin.m_value == id)
                {
                    hasError = true;
                    break;
                }
            }
            pin->SetTypeError(hasError);
        }

        for (auto& [id, node] : m_nodeMap)
        {
            bool hasError = false;
            QString tooltip;
            for (size_t i = 0; i < m_lastValidation.m_errors.Size(); ++i)
            {
                if (m_lastValidation.m_errors[i].m_node.m_value == id)
                {
                    hasError = true;
                    if (!tooltip.isEmpty())
                    {
                        tooltip += "\n";
                    }
                    tooltip += QString::fromUtf8(m_lastValidation.m_errors[i].m_message.CStr());
                }
            }
            node->SetHasError(hasError);
            node->SetErrorTooltip(hasError ? tooltip : QString{});
        }
    }

    BlueprintPin* BlueprintEditor::FindPinVisual(propolis::PinId id) const
    {
        auto it = m_pinMap.find(id.m_value);
        return it != m_pinMap.end() ? it->second : nullptr;
    }

    BlueprintNode* BlueprintEditor::FindNodeVisual(propolis::NodeId id) const
    {
        auto it = m_nodeMap.find(id.m_value);
        return it != m_nodeMap.end() ? it->second : nullptr;
    }

    BlueprintNode* BlueprintEditor::NodeVisual(propolis::NodeId id) const
    {
        return FindNodeVisual(id);
    }

    BlueprintConnection* BlueprintEditor::FindConnectionVisual(propolis::EdgeId id) const
    {
        auto it = m_edgeMap.find(id.m_value);
        return it != m_edgeMap.end() ? it->second : nullptr;
    }

    void BlueprintEditor::drawBackground(QPainter* painter, const QRectF& rect)
    {
        QGraphicsView::drawBackground(painter, rect);
        painter->setRenderHint(QPainter::Antialiasing, false);

        const qreal left = std::floor(rect.left() / GRID_SPACING) * GRID_SPACING;
        const qreal top = std::floor(rect.top() / GRID_SPACING) * GRID_SPACING;

        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor{0xff, 0xff, 0xff, 15});
        const qreal dotSize = 1.5;

        for (qreal x = left; x <= rect.right(); x += GRID_SPACING)
        {
            for (qreal y = top; y <= rect.bottom(); y += GRID_SPACING)
            {
                painter->drawRect(QRectF{x - dotSize * 0.5, y - dotSize * 0.5, dotSize, dotSize});
            }
        }

        const qreal majorSpacing = GRID_SPACING * MAJOR_GRID_EVERY;
        const qreal majorLeft = std::floor(rect.left() / majorSpacing) * majorSpacing;
        const qreal majorTop = std::floor(rect.top() / majorSpacing) * majorSpacing;
        const qreal majorDotSize = 2.5;

        painter->setBrush(QColor{0xff, 0xff, 0xff, 30});
        for (qreal x = majorLeft; x <= rect.right(); x += majorSpacing)
        {
            for (qreal y = majorTop; y <= rect.bottom(); y += majorSpacing)
            {
                painter->drawEllipse(QPointF{x, y}, majorDotSize * 0.5, majorDotSize * 0.5);
            }
        }
    }

    void BlueprintEditor::wheelEvent(QWheelEvent* event)
    {
        const double delta = event->angleDelta().y() / 120.0;
        const double factor = std::pow(1.15, delta);
        const double newZoom = m_zoom * factor;

        if (newZoom < MIN_ZOOM || newZoom > MAX_ZOOM)
        {
            return;
        }

        const QPointF scenePos = mapToScene(event->position().toPoint());
        scale(factor, factor);
        m_zoom = newZoom;

        const QPointF newScenePos = mapToScene(event->position().toPoint());
        const QPointF drift = newScenePos - scenePos;
        translate(drift.x(), drift.y());
    }

    void BlueprintEditor::mousePressEvent(QMouseEvent* event)
    {
        if (event->button() == Qt::MiddleButton)
        {
            m_isPanning = true;
            m_lastPanPos = event->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }

        if (event->button() == Qt::LeftButton)
        {
            QPointF scenePos = mapToScene(event->pos());
            BlueprintPin* pin = PinAt(scenePos);
            if (pin)
            {
                m_dragConn = new BlueprintDragConnection{pin};
                m_scene->addItem(m_dragConn);
                m_dragConn->SetEndPoint(scenePos);
                event->accept();
                return;
            }
        }

        if (event->button() == Qt::LeftButton)
        {
            SnapshotBeforeAction();
        }

        QGraphicsView::mousePressEvent(event);
    }

    void BlueprintEditor::mouseMoveEvent(QMouseEvent* event)
    {
        if (m_isPanning)
        {
            QPoint delta = event->pos() - m_lastPanPos;
            m_lastPanPos = event->pos();
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
            event->accept();
            return;
        }

        if (m_dragConn)
        {
            m_dragConn->SetEndPoint(mapToScene(event->pos()));
            event->accept();
            return;
        }

        QGraphicsView::mouseMoveEvent(event);
    }

    void BlueprintEditor::mouseReleaseEvent(QMouseEvent* event)
    {
        if (event->button() == Qt::MiddleButton && m_isPanning)
        {
            m_isPanning = false;
            setCursor(Qt::ArrowCursor);
            event->accept();
            return;
        }

        if (event->button() == Qt::LeftButton && m_dragConn)
        {
            FinalizeDrag(mapToScene(event->pos()));
            event->accept();
            return;
        }

        if (event->button() == Qt::LeftButton && m_snapshotBefore.Size() > 0)
        {
            bool moved = false;
            for (auto& [id, visual] : m_nodeMap)
            {
                propolis::Node* gn = m_graph.FindNode(propolis::NodeId{id});
                if (gn)
                {
                    float vx = static_cast<float>(visual->pos().x());
                    float vy = static_cast<float>(visual->pos().y());
                    if (vx != gn->m_posX || vy != gn->m_posY)
                    {
                        gn->m_posX = vx;
                        gn->m_posY = vy;
                        moved = true;
                    }
                }
            }
            if (moved)
            {
                PushUndoAfterAction();
            }
            else
            {
                m_snapshotBefore.Clear();
            }
        }

        QGraphicsView::mouseReleaseEvent(event);
    }

    void BlueprintEditor::keyPressEvent(QKeyEvent* event)
    {
        if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
        {
            DeleteSelected();
            event->accept();
            return;
        }

        if (event->key() == Qt::Key_F && !m_scene->selectedItems().isEmpty())
        {
            QRectF bounds;
            for (auto* item : m_scene->selectedItems())
            {
                bounds = bounds.united(item->sceneBoundingRect());
            }
            fitInView(bounds.adjusted(-50, -50, 50, 50), Qt::KeepAspectRatio);
            m_zoom = transform().m11();
            event->accept();
            return;
        }

        if (event->key() == Qt::Key_Z && (event->modifiers() & Qt::ControlModifier))
        {
            Undo();
            event->accept();
            return;
        }

        if (event->key() == Qt::Key_Y && (event->modifiers() & Qt::ControlModifier))
        {
            Redo();
            event->accept();
            return;
        }

        QGraphicsView::keyPressEvent(event);
    }

    void BlueprintEditor::contextMenuEvent(QContextMenuEvent* event)
    {
        ShowNodePalette(event->globalPos(), mapToScene(event->pos()));
    }

    void BlueprintEditor::dragEnterEvent(QDragEnterEvent* event)
    {
        if (event->mimeData()->hasFormat(kVariableDragMime))
        {
            event->acceptProposedAction();
        }
    }

    void BlueprintEditor::dragMoveEvent(QDragMoveEvent* event)
    {
        if (event->mimeData()->hasFormat(kVariableDragMime))
        {
            event->acceptProposedAction();
        }
    }

    void BlueprintEditor::dropEvent(QDropEvent* event)
    {
        if (!event->mimeData()->hasFormat(kVariableDragMime))
        {
            return;
        }

        uint32_t varId = event->mimeData()->data(kVariableDragMime).toUInt();
        propolis::VariableId vid{varId};
        const propolis::Variable* var = m_graph.FindVariable(vid);
        if (!var)
        {
            return;
        }

        QPointF scenePos = mapToScene(event->position().toPoint());
        bool makeSet = (event->modifiers() & Qt::AltModifier) != 0;
        const char* nodeType = makeSet ? "SetVariable" : "GetVariable";

        SnapshotBeforeAction();

        auto* visual = AddNodeFromRegistry(nodeType, scenePos);
        if (visual)
        {
            propolis::Node* graphNode = m_graph.FindNode(visual->PropolisId());
            if (graphNode)
            {
                graphNode->m_variableRef = vid;
            }
            QString prefix = makeSet ? "Set: " : "Get: ";
            visual->SetTitle(prefix + QString::fromUtf8(var->m_name.CStr()));
        }

        RunValidation();
        PushUndoAfterAction();
        event->acceptProposedAction();
    }

    void BlueprintEditor::ShowNodePalette(const QPoint& globalPos, const QPointF& scenePos)
    {
        const propolis::FunctionRegistry* fr = m_functionRegistryProvider
            ? m_functionRegistryProvider()
            : nullptr;
        BlueprintNodePalette::Show(this, globalPos, m_registry, fr,
            [this, scenePos](const PaletteSelection& sel) {
                SnapshotBeforeAction();
                if (sel.m_descriptor)
                {
                    (void)AddNodeFromRegistry(sel.m_descriptor->m_name.CStr(), scenePos);
                }
                else if (sel.m_function)
                {
                    (void)AddUserFunctionNode(*sel.m_function, scenePos);
                }
                RunValidation();
                PushUndoAfterAction();
            });
    }

    BlueprintPin* BlueprintEditor::PinAt(const QPointF& scenePos) const
    {
        auto items = m_scene->items(scenePos, Qt::IntersectsItemBoundingRect, Qt::DescendingOrder);
        for (auto* item : items)
        {
            if (auto* pin = dynamic_cast<BlueprintPin*>(item))
            {
                return pin;
            }
        }
        return nullptr;
    }

    bool BlueprintEditor::CanConnect(BlueprintPin* a, BlueprintPin* b) const
    {
        if (!a || !b)
        {
            return false;
        }
        if (a->Direction() == b->Direction())
        {
            return false;
        }
        if (a->Node() == b->Node())
        {
            return false;
        }
        if (a->IsExec() != b->IsExec())
        {
            return false;
        }
        return true;
    }

    void BlueprintEditor::FinalizeDrag(const QPointF& scenePos)
    {
        if (!m_dragConn)
        {
            return;
        }

        BlueprintPin* origin = m_dragConn->Origin();
        BlueprintPin* target = PinAt(scenePos);

        m_scene->removeItem(m_dragConn);
        delete m_dragConn;
        m_dragConn = nullptr;

        if (target && CanConnect(origin, target))
        {
            BlueprintPin* src = (origin->Direction() == propolis::PinDirection::OUTPUT) ? origin : target;
            BlueprintPin* dst = (origin->Direction() == propolis::PinDirection::OUTPUT) ? target : origin;
            ConnectPinsInternal(src, dst);
            RunValidation();
        }
    }

    void BlueprintEditor::RebuildVisualsFromGraph()
    {
        m_destroying = true;

        for (auto& [_, conn] : m_edgeMap)
        {
            conn->Detach();
        }

        m_scene->clear();
        m_destroying = false;

        m_pinMap.clear();
        m_nodeMap.clear();
        m_edgeMap.clear();

        for (size_t i = 0; i < m_graph.Nodes().Size(); ++i)
        {
            const propolis::Node& node = m_graph.Nodes()[i];
            const propolis::NodeDescriptor* desc = m_registry.Find(node.m_title.CStr());
            if (!desc)
            {
                if (node.m_userFn.m_qualifiedCppName.Size() > 0)
                {
                    QString cat = node.m_category.Size() > 0
                        ? QString::fromUtf8(node.m_category.CStr())
                        : QString{"User"};
                    (void)CreateUserFunctionVisual(node.m_id,
                                                    QString::fromUtf8(node.m_title.CStr()),
                                                    cat,
                                                    QPointF{node.m_posX, node.m_posY});
                }
                continue;
            }

            auto* visual = CreateNodeVisual(node.m_id, *desc, QPointF{node.m_posX, node.m_posY});

            if (visual)
            {
                if (node.m_variableRef &&
                    (propolis::HasFlag(desc->m_flags, propolis::NodeFlag::VARIABLE_GET) ||
                     propolis::HasFlag(desc->m_flags, propolis::NodeFlag::VARIABLE_SET)))
                {
                    const propolis::Variable* var = m_graph.FindVariable(node.m_variableRef);
                    if (var)
                    {
                        bool isSet = propolis::HasFlag(desc->m_flags, propolis::NodeFlag::VARIABLE_SET);
                        visual->SetTitle(QString{isSet ? "Set: %1" : "Get: %1"}.arg(
                            QString::fromUtf8(var->m_name.CStr())));
                    }
                }

                if (node.m_componentRef.Size() > 0 &&
                    (propolis::HasFlag(desc->m_flags, propolis::NodeFlag::COMPONENT_GET) ||
                     propolis::HasFlag(desc->m_flags, propolis::NodeFlag::COMPONENT_SET)))
                {
                    bool isSet = propolis::HasFlag(desc->m_flags, propolis::NodeFlag::COMPONENT_SET);
                    visual->SetTitle(QString{isSet ? "Set: %1" : "Get: %1"}.arg(
                        QString::fromUtf8(node.m_componentRef.CStr())));
                }
            }
        }

        for (size_t i = 0; i < m_graph.Edges().Size(); ++i)
        {
            const propolis::Edge& edge = m_graph.Edges()[i];
            BlueprintPin* srcPin = FindPinVisual(edge.m_source);
            BlueprintPin* dstPin = FindPinVisual(edge.m_target);
            if (srcPin && dstPin)
            {
                auto* conn = new BlueprintConnection{edge.m_id, srcPin, dstPin};
                m_scene->addItem(conn);
                m_edgeMap[edge.m_id.m_value] = conn;
            }
        }

        RunValidation();
    }

    static propolis::PType FieldTypeToPType(const queen::FieldInfo& field)
    {
        switch (field.m_type)
        {
        case queen::FieldType::BOOL:    return propolis::PType::Bool();
        case queen::FieldType::INT8:
        case queen::FieldType::INT16:
        case queen::FieldType::INT32:   return propolis::PType::Int32();
        case queen::FieldType::INT64:   return propolis::PType::Int32();
        case queen::FieldType::UINT8:
        case queen::FieldType::UINT16:
        case queen::FieldType::UINT32:  return propolis::PType::UInt32();
        case queen::FieldType::UINT64:  return propolis::PType::UInt32();
        case queen::FieldType::FLOAT32: return propolis::PType::Float32();
        case queen::FieldType::FLOAT64: return propolis::PType::Float64();
        case queen::FieldType::ENTITY:  return propolis::PType::Entity();
        case queen::FieldType::STRUCT:
        {
            if (field.m_nestedTypeId == queen::TypeIdOf<hive::math::Float2>()) return propolis::PType::Vec2();
            if (field.m_nestedTypeId == queen::TypeIdOf<hive::math::Float3>()) return propolis::PType::Vec3();
            if (field.m_nestedTypeId == queen::TypeIdOf<hive::math::Float4>()) return propolis::PType::Vec4();
            if (field.m_nestedTypeId == queen::TypeIdOf<hive::math::Quat>())   return propolis::PType::Quat();
            if (field.m_nestedTypeId == queen::TypeIdOf<hive::math::Mat4>())   return propolis::PType::Mat4();
            return propolis::PType{};
        }
        default: return propolis::PType{};
        }
    }

    void BlueprintEditor::RebuildComponentPins(propolis::Node* graphNode,
                                                const queen::ComponentRegistry<256>* registry)
    {
        if (!graphNode || !registry)
        {
            return;
        }

        const propolis::NodeDescriptor* desc = m_registry.Find(graphNode->m_title.CStr());
        if (!desc)
        {
            return;
        }

        bool isGet = propolis::HasFlag(desc->m_flags, propolis::NodeFlag::COMPONENT_GET);
        bool isSet = propolis::HasFlag(desc->m_flags, propolis::NodeFlag::COMPONENT_SET);
        if (!isGet && !isSet)
        {
            return;
        }

        auto& pins = isGet ? graphNode->m_outputs : graphNode->m_inputs;
        for (size_t i = pins.Size(); i > 0; --i)
        {
            const propolis::Pin* pin = m_graph.FindPin(pins[i - 1]);
            if (pin && pin->m_type.m_kind != propolis::PTypeKind::SIGNAL)
            {
                auto edges = m_graph.EdgesForPin(pins[i - 1]);
                for (size_t e = 0; e < edges.Size(); ++e)
                {
                    m_graph.RemoveEdge(edges[e]->m_id);
                }
                pins.EraseSwapBack(pins.Begin() + static_cast<ptrdiff_t>(i - 1));
            }
        }

        const queen::RegisteredComponent* comp = registry->FindByName(graphNode->m_componentRef.CStr());
        if (!comp || !comp->HasReflection())
        {
            return;
        }

        propolis::PinDirection dir = isGet ? propolis::PinDirection::OUTPUT : propolis::PinDirection::INPUT;
        const auto& refl = comp->m_reflection;
        for (size_t fi = 0; fi < refl.m_fieldCount; ++fi)
        {
            const queen::FieldInfo& field = refl.m_fields[fi];
            propolis::PType ptype = FieldTypeToPType(field);
            if (ptype.IsResolved())
            {
                (void)m_graph.AddPin(graphNode->m_id, dir, ptype, field.m_name);
            }
        }
    }

    namespace
    {
        struct BreakField
        {
            const char* m_name;
            propolis::PType m_type;
        };

        size_t GetBreakFields(propolis::PTypeKind kind, BreakField* out)
        {
            auto f32 = propolis::PType::Float32();
            switch (kind)
            {
            case propolis::PTypeKind::VEC2:
                out[0] = {"m_x", f32}; out[1] = {"m_y", f32};
                return 2;
            case propolis::PTypeKind::VEC3:
                out[0] = {"m_x", f32}; out[1] = {"m_y", f32}; out[2] = {"m_z", f32};
                return 3;
            case propolis::PTypeKind::VEC4:
            case propolis::PTypeKind::QUAT:
                out[0] = {"m_x", f32}; out[1] = {"m_y", f32};
                out[2] = {"m_z", f32}; out[3] = {"m_w", f32};
                return 4;
            default:
                return 0;
            }
        }
    } // namespace

    void BlueprintEditor::RebuildBreakPins(propolis::Node* graphNode)
    {
        if (!graphNode)
        {
            return;
        }

        for (size_t i = graphNode->m_outputs.Size(); i > 0; --i)
        {
            auto edges = m_graph.EdgesForPin(graphNode->m_outputs[i - 1]);
            for (size_t e = 0; e < edges.Size(); ++e)
            {
                m_graph.RemoveEdge(edges[e]->m_id);
            }
            graphNode->m_outputs.EraseSwapBack(
                graphNode->m_outputs.Begin() + static_cast<ptrdiff_t>(i - 1));
        }

        if (graphNode->m_inputs.Size() == 0)
        {
            return;
        }

        const propolis::Pin* inPin = m_graph.FindPin(graphNode->m_inputs[0]);
        if (!inPin)
        {
            return;
        }

        propolis::PType inputType = inPin->m_type;
        auto edges = m_graph.EdgesForPin(inPin->m_id);
        if (edges.Size() > 0)
        {
            const propolis::Pin* srcPin = m_graph.FindPin(edges[0]->m_source);
            if (srcPin && srcPin->m_type.IsResolved())
            {
                inputType = srcPin->m_type;
            }
        }

        BreakField fields[8];
        size_t fieldCount = GetBreakFields(inputType.m_kind, fields);
        for (size_t i = 0; i < fieldCount; ++i)
        {
            (void)m_graph.AddPin(graphNode->m_id, propolis::PinDirection::OUTPUT,
                                 fields[i].m_type, fields[i].m_name);
        }
    }

    void BlueprintEditor::RebuildMakePins(propolis::Node* graphNode)
    {
        if (!graphNode)
        {
            return;
        }

        for (size_t i = graphNode->m_inputs.Size(); i > 0; --i)
        {
            auto edges = m_graph.EdgesForPin(graphNode->m_inputs[i - 1]);
            for (size_t e = 0; e < edges.Size(); ++e)
            {
                m_graph.RemoveEdge(edges[e]->m_id);
            }
            graphNode->m_inputs.EraseSwapBack(
                graphNode->m_inputs.Begin() + static_cast<ptrdiff_t>(i - 1));
        }

        if (graphNode->m_outputs.Size() == 0)
        {
            return;
        }

        const propolis::Pin* outPin = m_graph.FindPin(graphNode->m_outputs[0]);
        if (!outPin)
        {
            return;
        }

        propolis::PType outputType = outPin->m_type;
        auto edges = m_graph.EdgesForPin(outPin->m_id);
        if (edges.Size() > 0)
        {
            const propolis::Pin* dstPin = m_graph.FindPin(edges[0]->m_target);
            if (dstPin && dstPin->m_type.IsResolved())
            {
                outputType = dstPin->m_type;
            }
        }

        BreakField fields[8];
        size_t fieldCount = GetBreakFields(outputType.m_kind, fields);
        for (size_t i = 0; i < fieldCount; ++i)
        {
            (void)m_graph.AddPin(graphNode->m_id, propolis::PinDirection::INPUT,
                                 fields[i].m_type, fields[i].m_name);
        }
    }

} // namespace forge
