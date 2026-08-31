#pragma once

#include <Caracal/Syntax/EnumDefinitionStatement.h>
#include <Caracal/Syntax/NodeIndex.h>
#include <Caracal/Semantic/Type.h>
#include <Caracal/Semantic/EnumField.h>
#include <Caracal/Text/TransparentStringHash.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace Caracal
{
    class EnumDefinition
    {
    public:
        EnumDefinition(
            const EnumDefinitionStatement* statement,
            Type type, 
            const std::string& name);

        [[nodiscard]] Type type() const noexcept { return m_type; }
        [[nodiscard]] Type baseType() const noexcept { return m_baseType; }
        [[nodiscard]] const std::string& name() const noexcept { return m_name; }
        [[nodiscard]] const EnumField* tryGetFieldByName(std::string_view fieldName) const noexcept;
        [[nodiscard]] bool hasField(std::string_view fieldName) const noexcept;
        [[nodiscard]] const std::vector<EnumField>& fields() const noexcept { return m_fields; }
        [[nodiscard]] const EnumDefinitionStatement* statement() const noexcept { return m_statement; }
        [[nodiscard]] NodeIndex definitionNode() const noexcept { return m_definitionNode; }
        void setDefinitionNode(NodeIndex definitionNode) noexcept { m_definitionNode = definitionNode; }

        void setBaseType(Type baseType) noexcept { m_baseType = baseType; }
        void addField(std::string_view name, Expression* expression, SourceLocation location) noexcept;
        void addField(std::string_view name, i32 value, SourceLocation location) noexcept;

    private:
        Type m_type;
        Type m_baseType;
        std::string m_name;
        std::vector<EnumField> m_fields;
        std::unordered_map<std::string, size_t, TransparentStringHash, std::equal_to<>> m_fieldIndices;
        const EnumDefinitionStatement* m_statement;
        NodeIndex m_definitionNode{};
    };
}
