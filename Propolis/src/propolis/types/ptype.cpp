#include <propolis/types/ptype.h>

namespace propolis
{
    bool PType::IsNumeric() const noexcept
    {
        switch (m_kind)
        {
        case PTypeKind::INT32:
        case PTypeKind::UINT32:
        case PTypeKind::FLOAT32:
        case PTypeKind::FLOAT64:
        case PTypeKind::VEC2:
        case PTypeKind::VEC3:
        case PTypeKind::VEC4:
            return true;
        default:
            return false;
        }
    }

    bool PType::IsOrdered() const noexcept
    {
        switch (m_kind)
        {
        case PTypeKind::INT32:
        case PTypeKind::UINT32:
        case PTypeKind::FLOAT32:
        case PTypeKind::FLOAT64:
            return true;
        default:
            return false;
        }
    }

    bool PType::IsInterpolable() const noexcept
    {
        switch (m_kind)
        {
        case PTypeKind::FLOAT32:
        case PTypeKind::FLOAT64:
        case PTypeKind::VEC2:
        case PTypeKind::VEC3:
        case PTypeKind::VEC4:
        case PTypeKind::QUAT:
            return true;
        default:
            return false;
        }
    }

    const char* PTypeKindName(PTypeKind kind) noexcept
    {
        switch (kind)
        {
        case PTypeKind::BOOL:      return "Bool";
        case PTypeKind::INT32:     return "Int32";
        case PTypeKind::UINT32:    return "UInt32";
        case PTypeKind::FLOAT32:   return "Float32";
        case PTypeKind::FLOAT64:   return "Float64";
        case PTypeKind::ENTITY:    return "Entity";
        case PTypeKind::VEC2:      return "Vec2";
        case PTypeKind::VEC3:      return "Vec3";
        case PTypeKind::VEC4:      return "Vec4";
        case PTypeKind::QUAT:      return "Quat";
        case PTypeKind::MAT4:      return "Mat4";
        case PTypeKind::SIGNAL:    return "Signal";
        case PTypeKind::STREAM:    return "Stream";
        case PTypeKind::OPTION:    return "Option";
        case PTypeKind::COMPONENT: return "Component";
        case PTypeKind::ENUM:      return "Enum";
        case PTypeKind::TYPE_VAR:  return "TypeVar";
        case PTypeKind::STRUCT:    return "Struct";
        }
        return "Unknown";
    }
} // namespace propolis
