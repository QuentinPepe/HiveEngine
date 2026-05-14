#pragma once

#include <wax/containers/vector.h>

#include <queen/core/entity.h>
#include <queen/core/type_id.h>
#include <queen/reflect/component_registry.h>

#include <cstdint>
#include <functional>

class QPoint;
class QWidget;

namespace queen
{
    class World;
}

namespace forge
{
    enum class AddComponentKind : uint8_t
    {
        COMPONENT,
        SCRIPT,
    };

    struct AddComponentChoice
    {
        AddComponentKind m_kind{AddComponentKind::COMPONENT};
        queen::TypeId m_componentTypeId{0};
        uint32_t m_scriptNameHash{0};
    };

    class AddComponentPopup
    {
    public:
        using AcceptFn = std::function<void(const AddComponentChoice&)>;

        // Single-entity flavour: hides components already on the entity.
        static void Show(QWidget* anchor, const QPoint& globalPos,
                         const queen::ComponentRegistry<256>& registry, queen::World& world, queen::Entity entity,
                         AcceptFn onAccept);

        // Multi-entity flavour: hides components that ALL selected entities already have;
        // shows components missing on at least one. The accept callback is responsible for
        // applying to each entity that needs it.
        static void ShowForEntities(QWidget* anchor, const QPoint& globalPos,
                                    const queen::ComponentRegistry<256>& registry, queen::World& world,
                                    const wax::Vector<queen::Entity>& entities, AcceptFn onAccept);
    };
} // namespace forge
