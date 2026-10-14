#pragma once

#include <Caracal/API.h>
#include <Caracal/Semantic/Type.h>
#include <Caracal/Syntax/NodeIndex.h>
#include <string>
#include <string_view>

namespace Caracal
{
    class CARACAL_API ConstantDefinition
    {
    public:
        ConstantDefinition(
            std::string_view name,
            Type type) noexcept;

        ConstantDefinition(
            std::string_view name,
            Type type,
            NodeIndex initializer) noexcept;

        [[nodiscard]] Type type() const noexcept;
        [[nodiscard]] const std::string& name() const noexcept;
        [[nodiscard]] NodeIndex initializerNode() const noexcept { return m_initializerNode; }
        [[nodiscard]] bool isInit() const noexcept;

    private:
        std::string m_name;
        Type m_type;
        NodeIndex m_initializerNode{};
        bool m_isInit{ false };
    };
}
