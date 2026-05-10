#include <propolis/compiler/cpp_type_mapper.h>

#include <cstdio>

namespace propolis
{
    const char* PTypeToCppType(PType type) noexcept
    {
        switch (type.m_kind)
        {
        case PTypeKind::BOOL:    return "bool";
        case PTypeKind::INT32:   return "int32_t";
        case PTypeKind::UINT32:  return "uint32_t";
        case PTypeKind::FLOAT32: return "float";
        case PTypeKind::FLOAT64: return "double";
        case PTypeKind::VEC2:    return "hive::math::Float2";
        case PTypeKind::VEC3:    return "hive::math::Float3";
        case PTypeKind::VEC4:    return "hive::math::Float4";
        case PTypeKind::QUAT:    return "hive::math::Quat";
        case PTypeKind::MAT4:    return "hive::math::Mat4";
        case PTypeKind::ENTITY:  return "queen::Entity";
        case PTypeKind::SIGNAL:  return "void";
        default:                 return "/* unknown */";
        }
    }

    const char* PTypeDefaultValue(PType type) noexcept
    {
        switch (type.m_kind)
        {
        case PTypeKind::BOOL:    return "false";
        case PTypeKind::INT32:   return "0";
        case PTypeKind::UINT32:  return "0u";
        case PTypeKind::FLOAT32: return "0.0f";
        case PTypeKind::FLOAT64: return "0.0";
        default:                 return "{}";
        }
    }

    wax::String SanitizeIdentifier(const wax::String& name)
    {
        wax::String result;
        for (size_t i = 0; i < name.Size(); ++i)
        {
            char c = name.CStr()[i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9' && i > 0) || c == '_')
            {
                result.Append(c);
            }
            else
            {
                result.Append('_');
            }
        }
        return result;
    }

    wax::String VarName(PinId pin)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "_v%u", pin.m_value);
        return wax::String{buf};
    }

    void AppendIndent(int level, wax::String& code)
    {
        for (int i = 0; i < level; ++i)
        {
            code.Append("    ");
        }
    }
} // namespace propolis
