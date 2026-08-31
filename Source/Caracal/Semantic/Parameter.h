#pragma once

#include <Caracal/Semantic/Type.h>
#include <Caracal/Syntax/NodeIndex.h>
#include <vector>
#include <string>

namespace Caracal
{
    class Expression;

    class Parameter
    {
    public:
        Parameter(
            std::string_view name,
            Type type,
            const Expression* defaultValue = nullptr);

        [[nodiscard]] const std::string& name() const noexcept { return m_name; }
        [[nodiscard]] Type type() const noexcept { return m_type; }
        [[nodiscard]] bool hasDefault() const noexcept { return m_defaultValue != nullptr || m_defaultValueNode.index != InvalidNodeIndex; }
        [[nodiscard]] const Expression* defaultValue() const noexcept { return m_defaultValue; }
        [[nodiscard]] bool hasDefaultNode() const noexcept { return m_defaultValueNode.index != InvalidNodeIndex; }
        [[nodiscard]] NodeIndex defaultValueNode() const noexcept { return m_defaultValueNode; }
        void setDefaultValueNode(NodeIndex node) noexcept { m_defaultValueNode = node; }

    private:
        std::string m_name;
        Type m_type;
        const Expression* m_defaultValue;
        NodeIndex m_defaultValueNode{};
    };
}
