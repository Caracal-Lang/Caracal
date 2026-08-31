#include "EnumField.h"

namespace Caracal
{
    EnumField::EnumField(std::string_view name, Expression* expression, SourceLocation location)
        : m_name{ name }
        , m_expression{ expression }
        , m_value{ 0 }
        , m_hasConstantValue{ false }
        , m_location{ location }
    {
    }

    EnumField::EnumField(std::string_view name, i32 value, SourceLocation location)
        : m_name{ name }
        , m_expression{ nullptr }
        , m_value{ value }
        , m_hasConstantValue{ true }
        , m_location{ location }
    {
    }
}
