#pragma once

#include <forge/blueprint_pin.h>

#include <propolis/graph/graph_types.h>

#include <QGraphicsItem>
#include <QColor>
#include <QString>

#include <vector>

namespace forge
{
    class BlueprintNode : public QGraphicsItem
    {
    public:
        BlueprintNode(propolis::NodeId propolisId, const QString& title, const QColor& titleColor,
                      QGraphicsItem* parent = nullptr);

        propolis::NodeId PropolisId() const { return m_propolisId; }
        const QString& Title() const { return m_title; }
        void SetTitle(const QString& title) { m_title = title; update(); }
        const std::vector<BlueprintPin*>& Inputs() const { return m_inputs; }
        const std::vector<BlueprintPin*>& Outputs() const { return m_outputs; }

        void AddPin(BlueprintPin* pin);
        void UpdateConnections();
        void LayoutPins();
        void SetHasError(bool hasError);
        void SetErrorTooltip(const QString& text);

        QRectF boundingRect() const override;
        void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    protected:
        QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

    private:
        static constexpr qreal WIDTH = 200.0;
        static constexpr qreal TITLE_HEIGHT = 28.0;
        static constexpr qreal PIN_ROW_HEIGHT = 24.0;
        static constexpr qreal PIN_MARGIN = 14.0;
        static constexpr qreal CORNER_RADIUS = 8.0;
        static constexpr qreal SHADOW_OFFSET = 3.0;

        propolis::NodeId m_propolisId;
        QString m_title;
        QColor m_titleColor;
        std::vector<BlueprintPin*> m_inputs;
        std::vector<BlueprintPin*> m_outputs;
        qreal m_bodyHeight{0};
        bool m_hasError{false};
    };
} // namespace forge
