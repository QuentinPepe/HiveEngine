#include <forge/blueprint_pin.h>
#include <forge/blueprint_connection.h>
#include <forge/blueprint_node.h>
#include <forge/theme.h>

#include <QPainter>
#include <QGraphicsSceneHoverEvent>

#include <algorithm>

namespace forge
{
    QColor PTypeColor(propolis::PType type)
    {
        switch (type.m_kind)
        {
        case propolis::PTypeKind::SIGNAL:  return {0xe8, 0xe8, 0xe8};
        case propolis::PTypeKind::BOOL:    return {0x8b, 0x1a, 0x1a};
        case propolis::PTypeKind::INT32:   return {0x1a, 0xbc, 0x9c};
        case propolis::PTypeKind::UINT32:  return {0x1a, 0x9c, 0xbc};
        case propolis::PTypeKind::FLOAT32: return {0x6c, 0xd4, 0x5e};
        case propolis::PTypeKind::FLOAT64: return {0x4a, 0xb4, 0x3e};
        case propolis::PTypeKind::VEC2:    return {0xf0, 0xc7, 0x30};
        case propolis::PTypeKind::VEC3:    return {0xf0, 0xc7, 0x30};
        case propolis::PTypeKind::VEC4:    return {0xf0, 0xc7, 0x30};
        case propolis::PTypeKind::QUAT:    return {0xb0, 0x87, 0xe0};
        case propolis::PTypeKind::MAT4:    return {0xb0, 0x87, 0xe0};
        case propolis::PTypeKind::ENTITY:  return {0x3d, 0x9a, 0xe8};
        case propolis::PTypeKind::COMPONENT: return {0x3d, 0x9a, 0xe8};
        case propolis::PTypeKind::ENUM:    return {0x1a, 0xbc, 0x9c};
        case propolis::PTypeKind::STREAM:  return {0xe0, 0x5d, 0xc5};
        case propolis::PTypeKind::STRUCT:  return {0x3d, 0x9a, 0xe8};
        case propolis::PTypeKind::TYPE_VAR: return {0x88, 0x88, 0x88};
        case propolis::PTypeKind::OPTION:  return {0x88, 0x88, 0x88};
        }
        return {0x88, 0x88, 0x88};
    }

    BlueprintPin::BlueprintPin(propolis::PinId propolisId, propolis::PinDirection direction,
                               propolis::PType type, bool isExec, const QString& name, BlueprintNode* parent)
        : QGraphicsItem{parent}
        , m_propolisId{propolisId}
        , m_direction{direction}
        , m_resolvedType{type}
        , m_isExec{isExec}
        , m_name{name}
        , m_node{parent}
    {
        setAcceptHoverEvents(true);
    }

    void BlueprintPin::SetResolvedType(propolis::PType type)
    {
        m_resolvedType = type;
        update();
    }

    void BlueprintPin::SetTypeError(bool hasError)
    {
        m_typeError = hasError;
        update();
    }

    void BlueprintPin::AddConnection(BlueprintConnection* conn)
    {
        m_connections.push_back(conn);
    }

    void BlueprintPin::RemoveConnection(BlueprintConnection* conn)
    {
        m_connections.erase(std::remove(m_connections.begin(), m_connections.end(), conn), m_connections.end());
    }

    QPointF BlueprintPin::CenterInScene() const
    {
        return mapToScene(0.0, 0.0);
    }

    QRectF BlueprintPin::boundingRect() const
    {
        return {-HIT_RADIUS, -HIT_RADIUS, HIT_RADIUS * 2.0, HIT_RADIUS * 2.0};
    }

    void BlueprintPin::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
    {
        painter->setRenderHint(QPainter::Antialiasing);

        QColor color = m_typeError ? QColor{0xe8, 0x4d, 0x4d} : PTypeColor(m_resolvedType);

        if (m_hovered)
        {
            QColor glow = color;
            glow.setAlpha(60);
            painter->setPen(Qt::NoPen);
            painter->setBrush(glow);
            painter->drawEllipse(QPointF{0, 0}, HIT_RADIUS, HIT_RADIUS);
        }

        if (m_isExec)
        {
            QPolygonF arrow;
            const qreal s = RADIUS + 1.0;
            if (m_direction == propolis::PinDirection::OUTPUT)
                arrow << QPointF{-s * 0.6, -s} << QPointF{s, 0} << QPointF{-s * 0.6, s};
            else
                arrow << QPointF{s * 0.6, -s} << QPointF{-s, 0} << QPointF{s * 0.6, s};

            painter->setPen(QPen{color, 1.5});
            if (IsConnected())
                painter->setBrush(color);
            else
                painter->setBrush(Qt::NoBrush);
            painter->drawPolygon(arrow);
        }
        else
        {
            painter->setPen(QPen{color, 1.5});
            if (IsConnected())
                painter->setBrush(color);
            else
                painter->setBrush(theme::kBackground);
            painter->drawEllipse(QPointF{0, 0}, RADIUS, RADIUS);
        }
    }

    void BlueprintPin::hoverEnterEvent(QGraphicsSceneHoverEvent*)
    {
        m_hovered = true;
        setToolTip(QString::fromUtf8(propolis::PTypeKindName(m_resolvedType.m_kind)));
        update();
    }

    void BlueprintPin::hoverLeaveEvent(QGraphicsSceneHoverEvent*)
    {
        m_hovered = false;
        update();
    }
} // namespace forge
