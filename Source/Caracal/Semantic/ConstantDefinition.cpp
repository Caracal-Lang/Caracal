#include "ConstantDefinition.h"

namespace Caracal
{
    ConstantDefinition::ConstantDefinition(
        std::string_view name,
        Type type) noexcept
        : m_name{ name }
        , m_type{ type }
        , m_isInit{ true }
    {
    }

    ConstantDefinition::ConstantDefinition(
        std::string_view name,
        Type type,
        NodeIndex initializer) noexcept
        : m_name{ name }
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

    bool ConstantDefinition::isInit() const noexcept
    {
        return m_isInit;
    }
}
