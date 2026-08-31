#pragma once

#include <Caracal/Optimization/OperatorFolding.h>
#include <Caracal/Syntax/BinaryExpression.h>
#include <Caracal/Syntax/TypeDefinitionStatement.h>
#include <Caracal/Syntax/NodeIndex.h>
#include <Caracal/Syntax/UnaryExpression.h>
#include <Caracal/Semantic/Type.h>
#include <Caracal/Semantic/FieldDefinition.h>
#include <Caracal/Text/TransparentStringHash.h>
#include <optional>
#include <string>
#include <unordered_map>
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
        TypeDefinition(
            const TypeDefinitionStatement* statement,
            Type type,
            const std::string& name);

        [[nodiscard]] Type type() const noexcept { return m_type; }
        [[nodiscard]] const std::string& name() const noexcept { return m_name; }
        void addField(Type fieldType, const std::string& fieldName, i32 fieldIndex, Expression* expression, bool isConstant = false) noexcept;
        [[nodiscard]] const FieldDefinition& tryGetFieldByName(std::string_view fieldName) const noexcept;
        [[nodiscard]] const std::vector<FieldDefinition>& fields() const noexcept { return m_fields; }
        void addMethod(Type methodType, const std::string& methodName) noexcept;
        [[nodiscard]] Type tryGetMethodTypeByName(std::string_view methodName) const noexcept;
        void addOperatorSignature(BinaryOperatorKind binaryOperator, const OperatorSignature& signature) noexcept;
        void addOperatorSignature(UnaryOperatorKind unaryOperator, const OperatorSignature& signature) noexcept;
        [[nodiscard]] const OperatorSignature* tryGetOperatorSignature(BinaryOperatorKind binaryOperator) const noexcept;
        [[nodiscard]] const OperatorSignature* tryGetOperatorSignature(UnaryOperatorKind unaryOperator) const noexcept;
        [[nodiscard]] const TypeDefinitionStatement* statement() const noexcept { return m_statement; }
        [[nodiscard]] NodeIndex definitionNode() const noexcept { return m_definitionNode; }
        void setDefinitionNode(NodeIndex definitionNode) noexcept { m_definitionNode = definitionNode; }
        
    private:
        Type m_type;
        std::string m_name;
        std::vector<FieldDefinition> m_fields;
        std::unordered_map<std::string, Type, TransparentStringHash, std::equal_to<>> m_methods;
        std::unordered_map<BinaryOperatorKind, OperatorSignature> m_binaryOperators;
        std::unordered_map<UnaryOperatorKind, OperatorSignature> m_unaryOperators;
        const TypeDefinitionStatement* m_statement;
        NodeIndex m_definitionNode{};
    };
}
