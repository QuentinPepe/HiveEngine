#pragma once

#include <propolis/graph/graph_types.h>

#include <QGraphicsPathItem>
#include <QColor>

namespace forge
{
    class BlueprintPin;

    class BlueprintConnection : public QGraphicsPathItem
    {
    public:
        BlueprintConnection(propolis::EdgeId propolisId, BlueprintPin* source, BlueprintPin* target,
                            QGraphicsItem* parent = nullptr);
        ~BlueprintConnection() override;

        propolis::EdgeId PropolisId() const { return m_propolisId; }
        BlueprintPin* Source() const { return m_source; }
        BlueprintPin* Target() const { return m_target; }

        void UpdatePath();
        void Detach() { m_source = nullptr; m_target = nullptr; }

        void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

        static QPainterPath MakeBezier(const QPointF& start, const QPointF& end);

    private:
        propolis::EdgeId m_propolisId;
        BlueprintPin* m_source;
        BlueprintPin* m_target;
    };

    class BlueprintDragConnection : public QGraphicsPathItem
    {
    public:
        explicit BlueprintDragConnection(BlueprintPin* origin, QGraphicsItem* parent = nullptr);

        BlueprintPin* Origin() const { return m_origin; }
        void SetEndPoint(const QPointF& scenePos);

        void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    private:
        BlueprintPin* m_origin;
        QPointF m_endPoint;
    };
} // namespace forge
