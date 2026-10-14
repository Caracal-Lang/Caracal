#include "EnumDefinition.h"

namespace Caracal 
{
    EnumDefinition::EnumDefinition(
        Type type, 
        const std::string& name)
        : m_type{ type }
        , m_baseType{ Type::Undefined() }
        , m_name{ name }
    {
    }
    
    const EnumField* EnumDefinition::tryGetFieldByName(std::string_view fieldName) const noexcept
    {
        const auto found = m_fieldIndices.find(fieldName);
        if (found == m_fieldIndices.end())
        {
            return nullptr;
        }

        return &m_fields[found->second];
    }

    bool EnumDefinition::hasField(std::string_view fieldName) const noexcept
    {
        return m_fieldIndices.contains(fieldName);
    }

    void EnumDefinition::addField(std::string_view name, SourceLocation location) noexcept
    {
        m_fieldIndices.emplace(std::string(name), m_fields.size());
        m_fields.emplace_back(name, location);
    }

    void EnumDefinition::addField(std::string_view name, i32 value, SourceLocation location) noexcept
    {
        m_fieldIndices.emplace(std::string(name), m_fields.size());
        m_fields.emplace_back(name, value, location);
    }
}
