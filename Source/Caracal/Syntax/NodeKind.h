#pragma once

#include <Caracal/API.h>
#include <Caracal/Defines.h>
#include <string>

namespace Caracal
{
    enum class NodeKind : u8
    {
        Unknown,
        Error,

        ConstantDeclaration,
        InitConstantDeclaration,
        VariableDeclaration,
        TypeFieldDeclaration,
        EnumFieldDeclaration,

        ExpressionStatement,
        AssignmentStatement,
        FunctionDefinitionStatement,
        EnumDefinitionStatement,
        TypeDefinitionStatement,
        MethodDefinitionStatement,
        IfStatement,
        TrailingIfStatement,
        WhileStatement,
        BreakStatement,
        SkipStatement,
        ReturnStatement,

        GroupingExpression,
        UnaryExpression,
        BinaryExpression,
        NameExpression,
        FunctionCallExpression,
        MemberAccessExpression,

        NamedArgument,

        DiscardLiteral,
        BoolLiteral,
        NumberLiteral,
        StringLiteral,
        ArrayLiteral,

        AnnotationNode,
        TypeNameNode,
        ArrayTypeNameNode,
        MethodNameNode,
        ParametersNode,
        ParameterNode,
        ReturnTypesNode,
        ReturnTypeNode,
        BlockNode,
    };

    [[nodiscard]] CARACAL_API std::string stringify(NodeKind kind);
}
