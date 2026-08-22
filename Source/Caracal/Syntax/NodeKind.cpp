#include "NodeKind.h"
#include <Caracal/Defines.h>
#include <unordered_map>

namespace Caracal
{
    std::string stringify(NodeKind kind)
    {
        static const std::unordered_map<NodeKind, std::string_view> kindToString{
            { NodeKind::Unknown,                        std::string_view("Unknown") },
            { NodeKind::Error,                          std::string_view("Error") },

            { NodeKind::ConstantDeclaration,            std::string_view("ConstantDeclaration") },
            { NodeKind::InitConstantDeclaration,        std::string_view("InitConstantDeclaration") },
            { NodeKind::VariableDeclaration,            std::string_view("VariableDeclaration") },
            { NodeKind::TypeFieldDeclaration,           std::string_view("TypeFieldDeclaration") },
            { NodeKind::EnumFieldDeclaration,           std::string_view("EnumFieldDeclaration") },

            { NodeKind::ExpressionStatement,            std::string_view("ExpressionStatement") },
            { NodeKind::AssignmentStatement,            std::string_view("AssignmentStatement") },
            { NodeKind::FunctionDefinitionStatement,    std::string_view("FunctionDefinitionStatement") },
            { NodeKind::EnumDefinitionStatement,        std::string_view("EnumDefinitionStatement") },
            { NodeKind::TypeDefinitionStatement,        std::string_view("TypeDefinitionStatement") },
            { NodeKind::MethodDefinitionStatement,      std::string_view("MethodDefinitionStatement") },
            { NodeKind::IfStatement,                    std::string_view("IfStatement") },
            { NodeKind::TrailingIfStatement,            std::string_view("TrailingIfStatement") },
            { NodeKind::WhileStatement,                 std::string_view("WhileStatement") },
            { NodeKind::BreakStatement,                 std::string_view("BreakStatement") },
            { NodeKind::SkipStatement,                  std::string_view("SkipStatement") },
            { NodeKind::ReturnStatement,                std::string_view("ReturnStatement") },

            { NodeKind::GroupingExpression,             std::string_view("GroupingExpression") },
            { NodeKind::UnaryExpression,                std::string_view("UnaryExpression") },
            { NodeKind::BinaryExpression,               std::string_view("BinaryExpression") },
            { NodeKind::NameExpression,                 std::string_view("NameExpression") },
            { NodeKind::FunctionCallExpression,         std::string_view("FunctionCallExpression") },
            { NodeKind::MemberAccessExpression,         std::string_view("MemberAccessExpression") },
            { NodeKind::NamedArgument,                  std::string_view("NamedArgument") },

            { NodeKind::DiscardLiteral,                 std::string_view("DiscardLiteral") },
            { NodeKind::BoolLiteral,                    std::string_view("BoolLiteral") },
            { NodeKind::NumberLiteral,                  std::string_view("NumberLiteral") },
            { NodeKind::StringLiteral,                  std::string_view("StringLiteral") },
            { NodeKind::ArrayLiteral,                   std::string_view("ArrayLiteral") },

            { NodeKind::AnnotationNode,                 std::string_view("AnnotationNode") },
            { NodeKind::TypeNameNode,                   std::string_view("TypeNameNode") },
            { NodeKind::ArrayTypeNameNode,              std::string_view("ArrayTypeNameNode") },
            { NodeKind::MethodNameNode,                 std::string_view("MethodNameNode") },
            { NodeKind::ParametersNode,                 std::string_view("ParametersNode") },
            { NodeKind::ParameterNode,                  std::string_view("ParameterNode") },
            { NodeKind::ReturnTypesNode,                std::string_view("ReturnTypesNode") },
            { NodeKind::ReturnTypeNode,                 std::string_view("ReturnTypeNode") },
            { NodeKind::BlockNode,                      std::string_view("BlockNode") },
        };

        const auto it = kindToString.find(kind);
        if (it != kindToString.end())
            return std::string(it->second);

        TODO("String for NodeKind value was not defined yet");
        return std::string();
    }
}
