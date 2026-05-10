#pragma once

#include <QFrame>
#include <QPoint>
#include <QPointF>

#include <functional>

namespace propolis
{
    class NodeRegistry;
    struct NodeDescriptor;
    class FunctionRegistry;
    struct FunctionEntry;
} // namespace propolis

namespace forge
{
    struct PaletteSelection
    {
        // Exactly one of these is non-null.
        const propolis::NodeDescriptor* m_descriptor{};
        const propolis::FunctionEntry* m_function{};
    };

    class BlueprintNodePalette
    {
    public:
        using AcceptFn = std::function<void(const PaletteSelection&)>;
        static void Show(QWidget* anchor, const QPoint& globalPos,
                         const propolis::NodeRegistry& registry,
                         const propolis::FunctionRegistry* functions,
                         AcceptFn onAccept);
    };
} // namespace forge
