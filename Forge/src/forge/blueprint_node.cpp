#include <forge/blueprint_node.h>
#include <forge/blueprint_connection.h>
#include <forge/theme.h>

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsDropShadowEffect>
#include <QFont>

#include <algorithm>

namespace forge
{
    BlueprintNode::BlueprintNode(propolis::NodeId propolisId, const QString& title,
                                 const QColor& titleColor, QGraphicsItem* parent)
        : QGraphicsItem{parent}
        , m_propolisId{propolisId}
        , m_title{title}
        , m_titleColor{titleColor}
    {
        setFlag(ItemIsMovable);
        setFlag(ItemIsSelectable);
        setFlag(ItemSendsGeometryChanges);

        auto* shadow = new QGraphicsDropShadowEffect{};
        shadow->setBlurRadius(20.0);
        shadow->setOffset(0.0, SHADOW_OFFSET);
        shadow->setColor(QColor{0, 0, 0, 80});
        setGraphicsEffect(shadow);
    }

    void BlueprintNode::AddPin(BlueprintPin* pin)
    {
        if (pin->Direction() == propolis::PinDirection::INPUT)
            m_inputs.push_back(pin);
        else
            m_outputs.push_back(pin);
        LayoutPins();
    }

    void BlueprintNode::LayoutPins()
    {
        const size_t rowCount = std::max(m_inputs.size(), m_outputs.size());
        m_bodyHeight = std::max(rowCount * PIN_ROW_HEIGHT + 8.0, PIN_ROW_HEIGHT + 8.0);

        for (size_t i = 0; i < m_inputs.size(); ++i)
        {
            qreal y = TITLE_HEIGHT + 4.0 + PIN_ROW_HEIGHT * 0.5 + static_cast<qreal>(i) * PIN_ROW_HEIGHT;
            m_inputs[i]->setPos(0.0, y);
        }

        for (size_t i = 0; i < m_outputs.size(); ++i)
        {
            qreal y = TITLE_HEIGHT + 4.0 + PIN_ROW_HEIGHT * 0.5 + static_cast<qreal>(i) * PIN_ROW_HEIGHT;
            m_outputs[i]->setPos(WIDTH, y);
        }
    }

    void BlueprintNode::UpdateConnections()
    {
        for (auto* pin : m_inputs)
            for (auto* conn : pin->Connections())
                conn->UpdatePath();
        for (auto* pin : m_outputs)
            for (auto* conn : pin->Connections())
                conn->UpdatePath();
    }

    QRectF BlueprintNode::boundingRect() const
    {
        const qreal totalH = TITLE_HEIGHT + m_bodyHeight;
        return {-1.0, -1.0, WIDTH + 2.0, totalH + 2.0 + SHADOW_OFFSET};
    }

    void BlueprintNode::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*)
    {
        painter->setRenderHint(QPainter::Antialiasing);

        const qreal totalH = TITLE_HEIGHT + m_bodyHeight;
        const QRectF rect{0, 0, WIDTH, totalH};
        const QRectF titleRect{0, 0, WIDTH, TITLE_HEIGHT};
        const bool selected = option->state & QStyle::State_Selected;

        QPainterPath bodyPath;
        bodyPath.addRoundedRect(rect, CORNER_RADIUS, CORNER_RADIUS);
        painter->setPen(Qt::NoPen);
        QColor bodyColor = theme::kSurface;
        bodyColor.setAlpha(0xf0);
        painter->setBrush(bodyColor);
        painter->drawPath(bodyPath);

        QPainterPath titleClip;
        titleClip.addRoundedRect(rect, CORNER_RADIUS, CORNER_RADIUS);
        QPainterPath titleFill;
        titleFill.addRect(titleRect);
        titleFill = titleFill.intersected(titleClip);

        QLinearGradient titleGrad{0, 0, 0, TITLE_HEIGHT};
        titleGrad.setColorAt(0.0, m_titleColor);
        QColor darker = m_titleColor.darker(160);
        darker.setAlpha(200);
        titleGrad.setColorAt(1.0, darker);
        painter->setBrush(titleGrad);
        painter->drawPath(titleFill);

        painter->setPen(QPen{QColor{0xff, 0xff, 0xff, 15}, 1.0});
        painter->drawLine(QPointF{CORNER_RADIUS, TITLE_HEIGHT}, QPointF{WIDTH - CORNER_RADIUS, TITLE_HEIGHT});

        QFont font{"Segoe UI", 9};
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(QColor{0xf0, 0xf0, 0xf0});
        painter->drawText(titleRect.adjusted(PIN_MARGIN, 0, -PIN_MARGIN, 0),
                          Qt::AlignVCenter | Qt::AlignLeft, m_title);

        QFont pinFont{"Segoe UI", 8};
        painter->setFont(pinFont);
        painter->setPen(QColor{0xcc, 0xcc, 0xcc});

        for (size_t i = 0; i < m_inputs.size(); ++i)
        {
            qreal y = TITLE_HEIGHT + 4.0 + static_cast<qreal>(i) * PIN_ROW_HEIGHT;
            QRectF labelRect{PIN_MARGIN + BlueprintPin::RADIUS + 4.0, y,
                             WIDTH * 0.5 - PIN_MARGIN, PIN_ROW_HEIGHT};
            painter->drawText(labelRect, Qt::AlignVCenter | Qt::AlignLeft, m_inputs[i]->Name());
        }

        for (size_t i = 0; i < m_outputs.size(); ++i)
        {
            qreal y = TITLE_HEIGHT + 4.0 + static_cast<qreal>(i) * PIN_ROW_HEIGHT;
            QRectF labelRect{WIDTH * 0.5, y,
                             WIDTH * 0.5 - PIN_MARGIN - BlueprintPin::RADIUS - 4.0, PIN_ROW_HEIGHT};
            painter->drawText(labelRect, Qt::AlignVCenter | Qt::AlignRight, m_outputs[i]->Name());
        }

        if (m_hasError)
        {
            painter->setPen(QPen{QColor{0xe0, 0x30, 0x30}, 2.5});
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(rect, CORNER_RADIUS, CORNER_RADIUS);
        }
        else if (selected)
        {
            painter->setPen(QPen{theme::kAccent, 2.0});
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(rect, CORNER_RADIUS, CORNER_RADIUS);
        }
        else
        {
            painter->setPen(QPen{QColor{0x3a, 0x3a, 0x3a, 0xc0}, 1.0});
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(rect, CORNER_RADIUS, CORNER_RADIUS);
        }
    }

    void BlueprintNode::SetHasError(bool hasError)
    {
        if (m_hasError != hasError)
        {
            m_hasError = hasError;
            update();
        }
    }

    void BlueprintNode::SetErrorTooltip(const QString& text)
    {
        setToolTip(text);
    }

    QVariant BlueprintNode::itemChange(GraphicsItemChange change, const QVariant& value)
    {
        if (change == ItemPositionHasChanged)
            UpdateConnections();
        return QGraphicsItem::itemChange(change, value);
    }
} // namespace forge
