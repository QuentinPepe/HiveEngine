#include <forge/blueprint_connection.h>
#include <forge/blueprint_pin.h>

#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include <cmath>

namespace forge
{
    static constexpr qreal CONNECTION_WIDTH = 2.5;
    static constexpr qreal CONNECTION_HOVER_WIDTH = 4.0;
    static constexpr qreal MIN_TANGENT = 50.0;
    static constexpr qreal TANGENT_RATIO = 0.5;

    QPainterPath BlueprintConnection::MakeBezier(const QPointF& start, const QPointF& end)
    {
        const qreal dx = std::abs(end.x() - start.x());
        const qreal tangent = std::max(MIN_TANGENT, dx * TANGENT_RATIO);

        QPainterPath path{start};
        path.cubicTo(
            start + QPointF{tangent, 0},
            end   + QPointF{-tangent, 0},
            end);
        return path;
    }

    BlueprintConnection::BlueprintConnection(propolis::EdgeId propolisId, BlueprintPin* source,
                                             BlueprintPin* target, QGraphicsItem* parent)
        : QGraphicsPathItem{parent}
        , m_propolisId{propolisId}
        , m_source{source}
        , m_target{target}
    {
        setZValue(-1.0);
        setAcceptHoverEvents(true);

        source->AddConnection(this);
        target->AddConnection(this);

        UpdatePath();
    }

    BlueprintConnection::~BlueprintConnection()
    {
        if (m_source)
            m_source->RemoveConnection(this);
        if (m_target)
            m_target->RemoveConnection(this);
    }

    void BlueprintConnection::UpdatePath()
    {
        if (!m_source || !m_target)
            return;
        setPath(MakeBezier(m_source->CenterInScene(), m_target->CenterInScene()));
    }

    void BlueprintConnection::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*)
    {
        painter->setRenderHint(QPainter::Antialiasing);

        QColor color = PTypeColor(m_source ? m_source->ResolvedType() : propolis::PType{});
        const bool hovered = option->state & QStyle::State_MouseOver;
        const qreal width = hovered ? CONNECTION_HOVER_WIDTH : CONNECTION_WIDTH;

        QColor shadow = color;
        shadow.setAlpha(30);
        painter->setPen(QPen{shadow, width + 3.0, Qt::SolidLine, Qt::RoundCap});
        painter->drawPath(path());

        painter->setPen(QPen{color, width, Qt::SolidLine, Qt::RoundCap});
        painter->drawPath(path());
    }

    BlueprintDragConnection::BlueprintDragConnection(BlueprintPin* origin, QGraphicsItem* parent)
        : QGraphicsPathItem{parent}
        , m_origin{origin}
        , m_endPoint{origin->CenterInScene()}
    {
        setZValue(10.0);
    }

    void BlueprintDragConnection::SetEndPoint(const QPointF& scenePos)
    {
        m_endPoint = scenePos;

        QPointF start = m_origin->CenterInScene();
        QPointF end = m_endPoint;

        if (m_origin->Direction() == propolis::PinDirection::INPUT)
            std::swap(start, end);

        setPath(BlueprintConnection::MakeBezier(start, end));
    }

    void BlueprintDragConnection::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
    {
        painter->setRenderHint(QPainter::Antialiasing);

        QColor color = PTypeColor(m_origin->ResolvedType());
        color.setAlpha(160);

        QPen pen{color, 2.0, Qt::DashLine, Qt::RoundCap};
        pen.setDashPattern({6, 4});
        painter->setPen(pen);
        painter->drawPath(path());
    }
} // namespace forge
