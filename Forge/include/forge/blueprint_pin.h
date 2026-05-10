#pragma once

#include <propolis/graph/graph_types.h>
#include <propolis/types/ptype.h>

#include <QGraphicsItem>
#include <QColor>
#include <QString>

#include <vector>

namespace forge
{
    class BlueprintNode;
    class BlueprintConnection;

    QColor PTypeColor(propolis::PType type);

    class BlueprintPin : public QGraphicsItem
    {
    public:
        static constexpr qreal RADIUS = 5.0;
        static constexpr qreal HIT_RADIUS = 10.0;

        BlueprintPin(propolis::PinId propolisId, propolis::PinDirection direction,
                     propolis::PType type, bool isExec, const QString& name, BlueprintNode* parent);

        propolis::PinId PropolisId() const { return m_propolisId; }
        propolis::PinDirection Direction() const { return m_direction; }
        propolis::PType ResolvedType() const { return m_resolvedType; }
        bool IsExec() const { return m_isExec; }
        const QString& Name() const { return m_name; }
        BlueprintNode* Node() const { return m_node; }

        void SetResolvedType(propolis::PType type);
        void SetTypeError(bool hasError);

        void AddConnection(BlueprintConnection* conn);
        void RemoveConnection(BlueprintConnection* conn);
        const std::vector<BlueprintConnection*>& Connections() const { return m_connections; }
        bool IsConnected() const { return !m_connections.empty(); }

        QPointF CenterInScene() const;

        QRectF boundingRect() const override;
        void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    protected:
        void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
        void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

    private:
        propolis::PinId m_propolisId;
        propolis::PinDirection m_direction;
        propolis::PType m_resolvedType;
        bool m_isExec;
        QString m_name;
        BlueprintNode* m_node;
        std::vector<BlueprintConnection*> m_connections;
        bool m_hovered{false};
        bool m_typeError{false};
    };
} // namespace forge
