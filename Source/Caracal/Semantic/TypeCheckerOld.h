#pragma once

#include <Caracal/API.h>
#include <Caracal/CompilationContext.h>
#include <Caracal/Defines.h>
#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/Semantic/AnnotationKind.h>
#include <Caracal/Semantic/TypeCheckerOptions.h>
#include <Caracal/Semantic/SemanticContext.h>
#include <Caracal/Semantic/Parameter.h>
#include <Caracal/Semantic/Scope.h>
#include <Caracal/Syntax/ParseTree.h>
#include <Caracal/Syntax/TokenBuffer.h>
#include <Caracal/Syntax/ArrayLiteral.h>
#include <Caracal/Syntax/ArrayTypeNameNode.h>
#include <Caracal/Syntax/ConstantDeclaration.h>
#include <Caracal/Syntax/FunctionDefinitionStatement.h>
#include <Caracal/Syntax/NumberLiteral.h>
#include <Caracal/Syntax/GroupingExpression.h>
#include <Caracal/Syntax/UnaryExpression.h>
#include <Caracal/Syntax/BinaryExpression.h>
#include <Caracal/Syntax/MemberAccessExpression.h>
#include <Caracal/Syntax/ReturnStatement.h>
#include <Caracal/Syntax/VariableDeclaration.h>
#include <Caracal/Syntax/NameExpression.h>
#include <Caracal/Syntax/IfStatement.h>
#include <Caracal/Syntax/WhileStatement.h>
#include <Caracal/Syntax/AssignmentStatement.h>
#include <Caracal/Syntax/FunctionCallExpression.h>
#include <Caracal/Syntax/ExpressionStatement.h>
#include <Caracal/Syntax/EnumDefinitionStatement.h>
#include <Caracal/Syntax/TypeDefinitionStatement.h>
#include <Caracal/Syntax/MethodDefinitionStatement.h>
#include <Caracal/Syntax/TypeFieldDeclaration.h>

#include <string>
#include <unordered_map>

namespace Caracal
{
    struct InitConstantDeclaration
    {
        SourceLocation location;
        SourceTextSharedPtr source;
    };

    class CARACAL_API TypeCheckerOld
    {
    public:
        TypeCheckerOld(
            CompilationContext& compilationContext,
            bool isPreludePass,
            DiagnosticsBag& diagnostics);

        CARACAL_DELETE_COPY_DELETE_MOVE(TypeCheckerOld)

        bool typeCheck();
        
    private:
        void collectDeclarations();
        void locateDefinitionNode(NodeIndex definitionNode, SourceTextSharedPtr& source, std::optional<SourceLocation>& location) const noexcept;
        void locateDefinitionNode(NodeIndex definitionNode, std::optional<SourceLocation>& location) const noexcept;
        void resolveWellKnownTypes();
        void collectMethodDeclarations();
        void typeCheckFunctionSignatures();
        void typeCheckTypeSignatures();
        void typeCheckGlobalConstants();
        void typeCheckFunctionDefinitions();
        void checkUninitializedInitConstants();
        void typeCheckEnumDefinitions();
        void typeCheckTypeFieldDefinitions();
        void typeCheckTypeMethodDefinitions();

        void typeCheckFunctionSignature(FunctionDefinitionStatement* statement, const TokenBuffer& tokens);
        void typeCheckTypeSignature(TypeDefinitionStatement* statement, const TokenBuffer& tokens);
        void typeCheckTypeFieldDefinition(TypeDefinitionStatement* statement, const TokenBuffer& tokens);
        void typeCheckTypeMethodDefinition(TypeDefinitionStatement* statement, const TokenBuffer& tokens);
        void typeCheckMethodSignature(const MethodDefinitionStatement* methodStatement, TypeDefinition& typeDefinition, Type typeType, const TokenBuffer& tokens);
        void validateStaticMethodTypeName(const MethodDefinitionStatement* methodStatement, TypeDefinition& typeDefinition, const TokenBuffer& tokens);
        void typeCheckConstructorSignature(const TypeDefinitionStatement* typeDefinitionStatement, TypeDefinition& typeDefinition, Type typeType, const TokenBuffer& tokens);
        void typeCheckStatement(Statement* statement, const TokenBuffer& tokens);
        void typeCheckConstantDeclaration(ConstantDeclaration* statement, const TokenBuffer& tokens);
        void typeCheckVariableDeclaration(VariableDeclaration* statement, const TokenBuffer& tokens);
        void typeCheckExpressionStatement(ExpressionStatement* statement, const TokenBuffer& tokens);
        void typeCheckAssignmentStatement(AssignmentStatement* statement, const TokenBuffer& tokens);
        void typeCheckFunctionDefinitionStatement(FunctionDefinitionStatement* statement, const TokenBuffer& tokens);
        void typeCheckEnumDefinitionStatement(EnumDefinitionStatement* statement, const TokenBuffer& tokens);
        void typeCheckTypeFieldDeclaration(TypeDefinition& typeDefinition, TypeFieldDeclaration* statement, i32 fieldIndex, const TokenBuffer& tokens);
        void typeCheckMethodDefinitionStatement(MethodDefinitionStatement* statement, const TokenBuffer& tokens);
        void typeCheckIfStatement(IfStatement* statement, const TokenBuffer& tokens);
        void typeCheckWhileStatement(WhileStatement* statement, const TokenBuffer& tokens);
        void typeCheckReturnStatement(ReturnStatement* statement, const TokenBuffer& tokens);
            
        [[nodiscard]] Type typeCheckExpression(Expression* expression, const TokenBuffer& tokens);
        [[nodiscard]] Type typeCheckGroupingExpression(GroupingExpression* expression, const TokenBuffer& tokens);
        [[nodiscard]] Type typeCheckUnaryExpressionExpression(UnaryExpression* unaryExpression, const TokenBuffer& tokens);
        [[nodiscard]] Type typeCheckBinaryExpressionExpression(BinaryExpression* binaryExpression, const TokenBuffer& tokens);
        [[nodiscard]] Type typeCheckNameExpression(NameExpression* nameExpression, const TokenBuffer& tokens);
        [[nodiscard]] Type typeCheckFunctionCallExpression(FunctionCallExpression* functionCallExpression, const TokenBuffer& tokens);
        [[nodiscard]] Type typeCheckResolvedFunctionCall(FunctionCallExpression* functionCallExpression, Type functionType, const TokenBuffer& tokens);
        [[nodiscard]] Type typeCheckMemberAccessExpression(MemberAccessExpression* memberAccessExpression, const TokenBuffer& tokens);
        [[nodiscard]] bool typeCheckCallArguments(FunctionCallExpression* functionCallExpression, const FunctionDefinition& functionDefinition, const TokenBuffer& tokens);
        [[nodiscard]] Type typeCheckNumberLiteral(NumberLiteral* literal, const TokenBuffer& tokens);
        [[nodiscard]] Type typeCheckArrayLiteral(ArrayLiteral* literal, const TokenBuffer& tokens);
        [[nodiscard]] Type typeCheckTypeNameNode(TypeNameNode* typeNameNode, const TokenBuffer& tokens);
        [[nodiscard]] Type typeCheckArrayTypeNameNode(ArrayTypeNameNode* arrayTypeNameNode, const TokenBuffer& tokens);
        [[nodiscard]] std::vector<Parameter> typeCheckParametersNode(ParametersNode* parametersNode, const TokenBuffer& tokens);
        [[nodiscard]] std::vector<Type> typeCheckReturnTypesNode(ReturnTypesNode* returnTypesNode, const TokenBuffer& tokens);

        void typeCheckBlockNode(BlockNode* body, const TokenBuffer& tokens);
        [[nodiscard]] i32 convertToI32(NumberLiteral* literal, const TokenBuffer& tokens);
        [[nodiscard]] Type coerceConditionType(Type conditionType, Expression* conditionExpression);
        [[nodiscard]] bool isAssignableTo(Type sourceType, Type targetType) const noexcept;
        [[nodiscard]] std::optional<Type> contextualTypeForExpectedType(Type expectedType) const noexcept;
        [[nodiscard]] bool tryAddArrayLengthMismatchError(const TokenBuffer& tokens, const SourceLocation& location, Type expectedType, Type actualType);
        [[nodiscard]] bool areComparableTypes(Type leftType, Type rightType);
        [[nodiscard]] bool validateAnnotation(const AnnotationNode* annotation, TokenKind targetKind, const TokenBuffer& tokens, std::optional<i32>* i32ArgumentValue = nullptr, std::optional<std::string>* stringArgumentValue = nullptr, bool* requiredValue = nullptr);
        [[nodiscard]] bool receiverChainIsConstant(const Expression* expression, std::string& rootName, bool& referencesConstant);
        [[nodiscard]] bool validateNamedAnnotationArguments(const AnnotationNode* annotation, std::string_view namedStringArgument, const TokenBuffer& tokens, std::optional<std::string>* stringArgumentValue, bool* requiredValue = nullptr);
        [[nodiscard]] bool validateBuiltinAnnotationArguments(const AnnotationNode* annotation, const TokenBuffer& tokens);
        [[nodiscard]] bool validateCallableAnnotations(const std::vector<AnnotationNodeUPtr>& annotations, const TokenBuffer& tokens, std::optional<std::string>& symbolName, bool& isRequired);
        void validateEnumAnnotation(const EnumDefinitionStatement* statement, const TokenBuffer& tokens, bool& isFlag, std::optional<i32>& stepValue);
        void validateTypeAnnotation(const TypeDefinitionStatement* statement, const TokenBuffer& tokens);
        void emitUnusedVariableWarnings(const Scope& scope);

        void pushScope(ScopeKind kind);
        void popScope(bool emitUnusedWarnings = false);
        [[nodiscard]] Scope* currentScope() const noexcept;
        
        const CompilationContext& m_compilationContext;
        const std::vector<ParseTreeUPtr>& m_parseTrees;
        TypeCheckerOptions m_options;
        bool m_isPreludePass = false;
        Type m_defaultIntegerType = Type::Undefined();
        Type m_defaultFloatingType = Type::Undefined();
        Type m_defaultEnumBaseType = Type::Undefined();
        SemanticContext& m_module;
        DiagnosticsBag& m_diagnostics;
        Type m_currentReturnType;
        Type m_currentType;
        std::optional<Type> m_contextualNumberType;
        bool m_leadingDotRootPending = false;
        bool m_negatedLiteralContext{ false };
        bool m_negatedLiteralSignConsumed{ false };
        std::vector<std::unique_ptr<Scope>> m_scopes;
        std::vector<std::unique_ptr<Scope>> m_scopePool;
        std::vector<const ConstantDeclaration*> m_globalConstantDeclarations;
        std::vector<const EnumDefinitionStatement*> m_enumDeclarations;
        std::vector<const TypeDefinitionStatement*> m_typeDeclarations;
        std::vector<const FunctionDefinitionStatement*> m_functionDeclarations;
        std::string m_currentFunctionName;
        std::unordered_map<std::string, InitConstantDeclaration> m_initConstants;
        std::unordered_map<std::string, SourceLocation> m_initConstantAssignments;
    };
};
