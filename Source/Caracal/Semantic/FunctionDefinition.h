#pragma once

#include <Caracal/Syntax/NodeIndex.h>
#include <Caracal/Semantic/Type.h>
#include <Caracal/Semantic/Parameter.h>
#include <optional>
#include <vector>
#include <string>

namespace Caracal
{
    enum class FunctionType
    {
        None,
        FreeFunction,
        StaticMethod,
        PublicMethod,
        PrivateMethod,
        SynthesizedConstructor,
        Intrinsic,
        Destructor
    };

    enum class IntrinsicKind
    {
        None,
        ArrayAt,
        ArraySet,
        ArrayAdd,
        ArrayRemove,
        ArraySlice,
        StringToCString,
        BitAnd,
        BitOr,
        BitXor,
        BitNot,
        ShiftLeft,
        ShiftRight,
    };

    [[nodiscard]] CARACAL_API bool isBitwiseIntrinsicKind(IntrinsicKind kind) noexcept;

    class FunctionDefinition
    {
    public:
        FunctionDefinition(
            Type type,
            Type parentType,
            FunctionType functionType,
            std::string name,
            std::string fullName,
            bool isVariadic,
            std::vector<Parameter> parameters = std::vector<Parameter>(),
            std::vector<Type> returnTypes = std::vector<Type>());

        [[nodiscard]] Type type() const noexcept { return m_type; }
        [[nodiscard]] Type parentType() const noexcept { return m_parentType; }
        [[nodiscard]] FunctionType functionType() const noexcept { return m_functionType; }
        void setFunctionType(FunctionType functionType) noexcept { m_functionType = functionType; }
        [[nodiscard]] IntrinsicKind intrinsicKind() const noexcept { return m_intrinsicKind; }
        void setIntrinsicKind(IntrinsicKind intrinsicKind) noexcept { m_intrinsicKind = intrinsicKind; }
        [[nodiscard]] const std::string& name() const noexcept { return m_name; }
        [[nodiscard]] const std::string& fullName() const noexcept { return m_fullName; }
        [[nodiscard]] bool isVariadic() const noexcept { return m_isVariadic; }
        [[nodiscard]] const std::vector<Parameter>& parameters() const noexcept { return m_parameters; }
        [[nodiscard]] const std::vector<Type>& returnTypes() const noexcept { return m_returnTypes; }
        [[nodiscard]] NodeIndex definitionNode() const noexcept { return m_definitionNode; }
        void setDefinitionNode(NodeIndex definitionNode) noexcept { m_definitionNode = definitionNode; }
        [[nodiscard]] const std::optional<std::string>& symbolName() const noexcept { return m_symbolName; }

        void setParameters(std::vector<Parameter> parameters) noexcept;
        void setReturnTypes(std::vector<Type> returnTypes) noexcept;
        void setIsVariadic(bool isVariadic) noexcept;
        void setSymbolName(std::optional<std::string> symbolName) noexcept { m_symbolName = std::move(symbolName); }

    private:
        Type m_type;
        Type m_parentType;
        FunctionType m_functionType;
        IntrinsicKind m_intrinsicKind{ IntrinsicKind::None };
        std::string m_name;
        std::string m_fullName;
        std::optional<std::string> m_symbolName;
        bool m_isVariadic;
        std::vector<Parameter> m_parameters;
        std::vector<Type> m_returnTypes;
        NodeIndex m_definitionNode{};
    };
}
