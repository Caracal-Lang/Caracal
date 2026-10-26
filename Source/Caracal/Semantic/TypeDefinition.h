#pragma once

#include <Caracal/Optimization/OperatorFolding.h>
#include <Caracal/Syntax/BinaryOperatorKind.h>
#include <Caracal/Syntax/NodeIndex.h>
#include <Caracal/Syntax/UnaryOperatorKind.h>
#include <Caracal/Semantic/Type.h>
#include <Caracal/Semantic/FieldDefinition.h>
#include <Caracal/Text/TransparentStringHash.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <type_traits>
#include <vector>

namespace Caracal
{
    struct OperatorSignature
    {
        Type lhsType;
        Type rhsType;
        Type resultType;
        BinaryFoldFunction binaryFold = nullptr;
        UnaryFoldFunction unaryFold = nullptr;
        Type callee = Type::Undefined();
    };

    class TypeDefinition
    {
    public:
        using MethodsByName = std::unordered_map<std::string, Type, TransparentStringHash, std::equal_to<>>;

        TypeDefinition(
            Type type,
            const std::string& name);
        TypeDefinition(const TypeDefinition&) = default;
        TypeDefinition& operator=(const TypeDefinition&) = default;
        TypeDefinition(TypeDefinition&&) noexcept = default;
        TypeDefinition& operator=(TypeDefinition&&) noexcept = default;

        [[nodiscard]] Type type() const noexcept { return m_type; }
        [[nodiscard]] const std::string& name() const noexcept { return m_name; }
        void addField(Type fieldType, const std::string& fieldName, i32 fieldIndex, bool isConstant = false) noexcept;
        [[nodiscard]] const FieldDefinition& tryGetFieldByName(std::string_view fieldName) const noexcept;
        [[nodiscard]] const std::vector<FieldDefinition>& fields() const noexcept { return m_fields; }
        void addMethod(Type methodType, const std::string& methodName) noexcept;
        [[nodiscard]] const MethodsByName& methods() const noexcept { return m_methods; }
        [[nodiscard]] Type tryGetMethodTypeByName(std::string_view methodName) const noexcept;
        void addOperatorSignature(BinaryOperatorKind binaryOperator, const OperatorSignature& signature) noexcept;
        void addOperatorSignature(UnaryOperatorKind unaryOperator, const OperatorSignature& signature) noexcept;
        [[nodiscard]] const OperatorSignature* tryGetOperatorSignature(BinaryOperatorKind binaryOperator) const noexcept;
        [[nodiscard]] const OperatorSignature* tryGetOperatorSignature(UnaryOperatorKind unaryOperator) const noexcept;
        [[nodiscard]] NodeIndex definitionNode() const noexcept { return m_definitionNode; }
        void setDefinitionNode(NodeIndex definitionNode) noexcept { m_definitionNode = definitionNode; }
        
    private:
        Type m_type;
        std::string m_name;
        std::vector<FieldDefinition> m_fields;
        MethodsByName m_methods;
        std::unordered_map<BinaryOperatorKind, OperatorSignature> m_binaryOperators;
        std::unordered_map<UnaryOperatorKind, OperatorSignature> m_unaryOperators;
        NodeIndex m_definitionNode{};
    };

    static_assert(std::is_nothrow_move_constructible_v<TypeDefinition>, "a growing std::vector would copy every TypeDefinition");
}
