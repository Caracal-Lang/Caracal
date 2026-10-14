#include "EnumField.h"

namespace Caracal
{
    EnumField::EnumField(std::string_view name, SourceLocation location)
        : m_name{ name }
        , m_value{ 0 }
        , m_hasConstantValue{ false }
        , m_location{ location }
    {
    }

    EnumField::EnumField(std::string_view name, i32 value, SourceLocation location)
        : m_name{ name }
        , m_value{ value }
        , m_hasConstantValue{ true }
        , m_location{ location }
    {
    }
}
