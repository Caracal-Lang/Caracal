#pragma once

#include <Caracal/API.h>
#include <Caracal/Defines.h>
#include <Caracal/Syntax/ArrayLiteral.h>
#include <Caracal/Syntax/ArrayTypeNameNode.h>
#include <Caracal/Syntax/BinaryExpression.h>
#include <Caracal/Syntax/BlockNode.h>
#include <Caracal/Syntax/EnumDefinitionStatement.h>
#include <Caracal/Syntax/EnumFieldDeclaration.h>
#include <Caracal/Syntax/ExpressionStatement.h>
#include <Caracal/Syntax/ConstantDeclaration.h>
#include <Caracal/Syntax/VariableDeclaration.h>
#include <Caracal/Syntax/FunctionDefinitionStatement.h>
#include <Caracal/Syntax/GroupingExpression.h>
#include <Caracal/Syntax/IfStatement.h>
#include <Caracal/Syntax/MethodDefinitionStatement.h>
#include <Caracal/Syntax/NameExpression.h>
#include <Caracal/Syntax/MethodNameNode.h>
#include <Caracal/Syntax/ParametersNode.h>
#include <Caracal/Syntax/ParseTree.h>
#include <Caracal/Syntax/ReturnStatement.h>
#include <Caracal/Syntax/DiscardLiteral.h>
#include <Caracal/Syntax/BoolLiteral.h>
#include <Caracal/Syntax/StringLiteral.h>
#include <Caracal/Syntax/NumberLiteral.h>
#include <Caracal/Syntax/AssignmentStatement.h>
#include <Caracal/Syntax/UnaryExpression.h>
#include <Caracal/Syntax/FunctionCallExpression.h>
#include <Caracal/Syntax/WhileStatement.h>
#include <Caracal/Syntax/BreakStatement.h>
#include <Caracal/Syntax/SkipStatement.h>
#include <Caracal/Syntax/TypeDefinitionStatement.h>
#include <Caracal/Syntax/TypeFieldDeclaration.h>
#include <Caracal/Syntax/MemberAccessExpression.h>
#include <Caracal/Text/StringBuilder.h>
#include <Caracal/Semantic/SemanticContext.h>

namespace Caracal 
{
    class CARACAL_API ParseTreePrinterOld
    {
    public:
        ParseTreePrinterOld(
            const ParseTree& parseTree, 
            SemanticContext* module, 
            i32 indentation = 4);

        CARACAL_DELETE_COPY_DELETE_MOVE(ParseTreePrinterOld)

        [[nodiscard]] std::string prettyPrint();

    private:
        void prettyPrintNode(Node* node);
        void prettyPrintExpressionStatement(ExpressionStatement* statement);
        void prettyPrintConstantDeclaration(ConstantDeclaration* statement);
        void prettyPrintVariableDeclaration(VariableDeclaration* statement);
        void prettyPrintTypeFieldDeclaration(TypeFieldDeclaration* statement);
        void prettyPrintAssignmentStatement(AssignmentStatement* statement);
        void prettyPrintTypeDefinitionStatement(TypeDefinitionStatement* statement);
        void prettyPrintFunctionDefinitionStatement(FunctionDefinitionStatement* statement);
        void prettyPrintEnumDefinitionStatement(EnumDefinitionStatement* statement);
        void prettyPrintEnumFieldDeclaration(EnumFieldDeclaration* statement);
        void prettyPrintMethodDefinitionStatement(MethodDefinitionStatement* statement);
        void prettyPrintIfStatement(IfStatement* statement);
        void prettyPrintWhileStatement(WhileStatement* statement);
        void prettyPrintBreakStatement(BreakStatement* statement);
        void prettyPrintSkipStatement(SkipStatement* statement);
        void prettyPrintReturnStatement(ReturnStatement* statement);
        void prettyPrintArgumentList(const std::vector<Argument>& arguments);
        void prettyPrintParameterNode(ParameterNode* parameter);
        void prettyPrintParametersNode(ParametersNode* parameters);
        void prettyPrintReturnTypesNode(ReturnTypesNode* returnTypes);
        void prettyPrintTypeNameNode(TypeNameNode* returnType);
        void prettyPrintArrayTypeNameNode(ArrayTypeNameNode* node);
        void prettyPrintArrayLiteral(ArrayLiteral* node);
        void prettyPrintBlockNode(BlockNode* block);
        void prettyPrintGroupingExpression(GroupingExpression* grouping);
        void prettyPrintUnaryExpression(UnaryExpression* unaryExpression);
        void prettyPrintBinaryExpression(BinaryExpression* binaryExpression);
        void prettyPrintNameExpression(NameExpression* name);
        void prettyPrintMethodNameNode(MethodNameNode* node);
        void prettyPrintFunctionCallExpression(FunctionCallExpression* functionCall);
        void prettyPrintMemberAccessExpression(MemberAccessExpression* expression);
        void prettyPrintDiscardLiteral(DiscardLiteral* discard);
        void prettyPrintBoolLiteral(BoolLiteral* node);
        void prettyPrintNumberLiteral(NumberLiteral* number);
        void prettyPrintStringLiteral(StringLiteral* string);
        void prettyPrintAnnotation(AnnotationNode* annotation);

        void writeIndentedTypeName(Type type, std::string_view = "Type: ");

        const ParseTree& m_parseTree;
        SemanticContext* m_module;
        StringBuilder m_builder;
    };
}
