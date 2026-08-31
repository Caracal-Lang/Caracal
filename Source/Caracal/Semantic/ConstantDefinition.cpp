#include "ConstantDefinition.h"

namespace Caracal
{
    ConstantDefinition::ConstantDefinition(
        std::string_view name,
        const Expression* expression) noexcept
        : m_name{ name }
        , m_expression{ expression }
        , m_type{ expression->type() }
    {
    }

    ConstantDefinition::ConstantDefinition(
        std::string_view name,
        Type type) noexcept
        : m_name{ name }
        , m_expression{ nullptr }
        , m_type{ type }
        , m_isInit{ true }
    {
    }

    ConstantDefinition::ConstantDefinition(
        std::string_view name,
        Type type,
        NodeIndex initializer) noexcept
        : m_name{ name }
        , m_expression{ nullptr }
        , m_type{ type }
        , m_initializerNode{ initializer }
    {
    }

    Type ConstantDefinition::type() const noexcept
    {
        return m_type;
    }

    const std::string& ConstantDefinition::name() const noexcept
    {
        return m_name;
    }

    const Expression* ConstantDefinition::expression() const noexcept
    {
        return m_expression;
    }

    bool ConstantDefinition::isInit() const noexcept
    {
        return m_isInit;
    }
}
