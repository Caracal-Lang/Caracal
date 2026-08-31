#pragma once

#include <Caracal/API.h>
#include <Caracal/CompilationContext.h>
#include <Caracal/Defines.h>
#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/Semantic/BuiltinTypeDescription.h>
#include <Caracal/Semantic/Parameter.h>
#include <Caracal/Semantic/Scope.h>
#include <Caracal/Semantic/SemanticContext.h>
#include <Caracal/Semantic/TypeCheckerOptions.h>
#include <Caracal/Syntax/NodeBuffer.h>
#include <Caracal/Syntax/NodeIndex.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Caracal
{
    class CARACAL_API TypeChecker
    {
    public:
        TypeChecker(
            CompilationContext& compilationContext,
            bool isPreludePass,
            DiagnosticsBag& diagnostics);

        CARACAL_DELETE_COPY_DELETE_MOVE(TypeChecker)

        [[nodiscard]] bool typeCheck();

    private:
        struct ExpressionContext
        {
            std::optional<Type> contextualNumberType;
            bool negatedLiteral = false;
        };

        struct InitConstantRecord
        {
            SourceLocation location;
            SourceTextSharedPtr source;
        };

        struct LexicalBinding
        {
            Type type;
            SourceTextSharedPtr source;
            std::optional<SourceLocation> location;
            VariableBindingKind kind = VariableBindingKind::LocalVariable;
            bool wasRead = false;
            bool referencesConstant = false;
            u32 scopeIndex = 0;
        };

        struct ScopeFrame
        {
            u32 boundNamesStart = 0;
            ScopeKind kind = ScopeKind::Invalid;
        };

        void collectDeclarations();
        void collectUnitDeclarations(CompilationUnit& unit);
        void collectFunctionDeclaration(const CompilationUnit& unit, u32 index);
        void collectEnumDeclaration(const CompilationUnit& unit, u32 index);
        void collectTypeDeclaration(const CompilationUnit& unit, u32 index);
        void collectBuiltinTypeDeclaration(const CompilationUnit& unit, u32 index);
        [[nodiscard]] BuiltinTypeDescription extractPendingBuiltinTypeDescription(const CompilationUnit& unit) const;
        void reportDuplicateTypeDeclaration(const CompilationUnit& unit, u32 index, std::string_view name, Type existingType);
        void resolveWellKnownTypes();
        void checkUninitializedInitConstants();

        void collectMethodDeclarations();
        void typeCheckFunctionSignatures();
        void typeCheckTypeSignatures();
        void typeCheckFunctionSignature(const CompilationUnit& unit, u32 index);
        void typeCheckTypeSignature(const CompilationUnit& unit, u32 index);
        void typeCheckMethodSignature(const CompilationUnit& unit, u32 methodIndex, TypeDefinition& typeDefinition, Type typeType);
        void validateStaticMethodTypeName(const CompilationUnit& unit, u32 methodIndex, TypeDefinition& typeDefinition);
        void typeCheckConstructorSignature(const CompilationUnit& unit, u32 typeIndex, TypeDefinition& typeDefinition, Type typeType);
        [[nodiscard]] Type typeCheckTypeNameNode(const CompilationUnit& unit, u32 index);
        [[nodiscard]] Type typeCheckArrayTypeNameNode(const CompilationUnit& unit, u32 index);
        [[nodiscard]] std::vector<Parameter> typeCheckParametersNode(const CompilationUnit& unit, u32 index);
        [[nodiscard]] std::vector<Type> typeCheckReturnTypesNode(const CompilationUnit& unit, u32 index);
        [[nodiscard]] i32 convertToI32(const CompilationUnit& unit, u32 numberLiteralIndex);
        void pushScope(ScopeKind kind);
        void popScope(bool emitUnusedWarnings = false);
        [[nodiscard]] ScopeKind currentScopeKind() const noexcept;
        [[nodiscard]] bool currentScopeIsGlobal() const noexcept;
        void emitUnusedVariableWarnings(const ScopeFrame& frame);
        [[nodiscard]] u32 internName(std::string_view lexeme);
        void internUnitIdentifiers(const CompilationUnit& unit);
        [[nodiscard]] u32 nameIdOf(const CompilationUnit& unit, u32 index) const noexcept;
        void addVariableBinding(u32 nameId, Type type, std::optional<SourceLocation> location, const SourceTextSharedPtr& source, VariableBindingKind kind, bool referencesConstant = false);
        [[nodiscard]] const LexicalBinding* tryGetBinding(u32 nameId) const noexcept;
        [[nodiscard]] LexicalBinding* tryGetBindingMutable(u32 nameId) noexcept;

        void typeCheckGlobalConstants();
        void typeCheckEnumDefinitions();
        void typeCheckTypeFieldDefinitions();
        void typeCheckFunctionDefinitions();
        void typeCheckTypeMethodDefinitions();
        void typeCheckStatement(const CompilationUnit& unit, u32 index);
        void typeCheckConstantDeclaration(const CompilationUnit& unit, u32 index);
        void typeCheckInitConstantDeclaration(const CompilationUnit& unit, u32 index);
        void typeCheckVariableDeclaration(const CompilationUnit& unit, u32 index);
        void typeCheckExpressionStatement(const CompilationUnit& unit, u32 index);
        void typeCheckAssignmentStatement(const CompilationUnit& unit, u32 index);
        void typeCheckFunctionDefinitionStatement(const CompilationUnit& unit, u32 index);
        void typeCheckEnumDefinitionStatement(const CompilationUnit& unit, u32 index);
        void typeCheckTypeFieldDeclaration(const CompilationUnit& unit, u32 index, TypeDefinition& typeDefinition, i32 fieldIndex);
        void typeCheckMethodDefinitionStatement(const CompilationUnit& unit, u32 index);
        void typeCheckIfStatement(const CompilationUnit& unit, u32 index);
        void typeCheckTrailingIfStatement(const CompilationUnit& unit, u32 index);
        void typeCheckWhileStatement(const CompilationUnit& unit, u32 index);
        void typeCheckReturnStatement(const CompilationUnit& unit, u32 index);
        void typeCheckBlockNode(const CompilationUnit& unit, u32 index);
        [[nodiscard]] bool receiverChainIsConstant(const CompilationUnit& unit, u32 expressionIndex, std::string& rootName, bool& referencesConstant);

        [[nodiscard]] Type typeCheckExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context = {});
        [[nodiscard]] Type typeCheckGroupingExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context);
        [[nodiscard]] Type typeCheckUnaryExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context);
        [[nodiscard]] Type typeCheckBinaryExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context);
        [[nodiscard]] Type typeCheckNameExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context);
        [[nodiscard]] Type typeCheckFunctionCallExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context);
        [[nodiscard]] Type typeCheckResolvedFunctionCall(const CompilationUnit& unit, u32 index, Type functionType, const ExpressionContext& context);
        [[nodiscard]] Type typeCheckMemberAccessExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context);
        [[nodiscard]] Type finishResolvedMethodCall(const CompilationUnit& unit, u32 index, u32 callIndex, u32 nameExpressionIndex, Type methodType, const ExpressionContext& context);
        [[nodiscard]] bool typeCheckCallArguments(const CompilationUnit& unit, u32 callIndex, Type functionType, const ExpressionContext& context);
        [[nodiscard]] Type typeCheckNumberLiteral(const CompilationUnit& unit, u32 index, const ExpressionContext& context);
        [[nodiscard]] Type typeCheckArrayLiteral(const CompilationUnit& unit, u32 index, const ExpressionContext& context);
        [[nodiscard]] Type coerceConditionType(Type conditionType, const CompilationUnit& unit, u32 conditionIndex);
        [[nodiscard]] bool isAssignableTo(Type sourceType, Type targetType) const noexcept;
        [[nodiscard]] std::optional<Type> contextualTypeForExpectedType(Type expectedType) const noexcept;
        [[nodiscard]] bool tryAddArrayLengthMismatchError(const CompilationUnit& unit, const SourceLocation& location, Type expectedType, Type actualType);
        [[nodiscard]] bool areComparableTypes(Type leftType, Type rightType);

        [[nodiscard]] bool validateAnnotation(const CompilationUnit& unit, u32 annotationIndex, TokenKind targetKind, std::optional<i32>* i32ArgumentValue = nullptr, std::optional<std::string>* stringArgumentValue = nullptr, bool* requiredValue = nullptr);
        [[nodiscard]] bool validateBuiltinAnnotationArguments(const CompilationUnit& unit, u32 annotationIndex);
        [[nodiscard]] bool validateNamedAnnotationArguments(const CompilationUnit& unit, u32 annotationIndex, std::string_view namedStringArgument, std::optional<std::string>* stringArgumentValue, bool* requiredValue = nullptr);
        [[nodiscard]] bool validateCallableAnnotations(const CompilationUnit& unit, const std::vector<u32>& annotations, std::optional<std::string>& symbolName, bool& isRequired);
        void validateEnumAnnotation(const CompilationUnit& unit, u32 enumIndex, const std::vector<u32>& annotations, bool& isFlag, std::optional<i32>& stepValue);
        void validateTypeAnnotation(const CompilationUnit& unit, u32 typeIndex, const std::vector<u32>& annotations);

        [[nodiscard]] std::vector<u32> collectRootStatements(const NodeBuffer& buffer) const;
        [[nodiscard]] std::vector<Parameter> collectParameterNames(const CompilationUnit& unit, u32 parametersNode);
        [[nodiscard]] Token tokenAt(u16 fileId, u32 tokenIndex) const noexcept;
        [[nodiscard]] std::string_view nameOf(const CompilationUnit& unit, u32 index) const noexcept;
        [[nodiscard]] const SourceLocation& nameLocation(const CompilationUnit& unit, u32 index) const noexcept;
        [[nodiscard]] SourceLocation annotationLocation(const CompilationUnit& unit, u32 index) const noexcept;
        void locateDefinitionName(
            NodeIndex definitionNode,
            const std::optional<Token>& statementNameToken,
            SourceTextSharedPtr& source,
            std::optional<SourceLocation>& location) const noexcept;
        void collectMethodAnnotationsBefore(const CompilationUnit& unit, u32 methodIndex, const TypeDefinition& typeDefinition, std::vector<u32>& annotations) const;
        [[nodiscard]] bool pendingAnnotationsContain(const CompilationUnit& unit, std::string_view name) const noexcept;

        Type noteType(const CompilationUnit& unit, u32 index, Type type) noexcept
        {
            m_module.setNodeType(NodeIndex{ unit.fileId, index }, type);
            return type;
        }

        [[nodiscard]] CompilationUnit& unitFor(NodeIndex index) const noexcept;

        CompilationContext& m_compilationContext;
        bool m_isPreludePass;
        SemanticContext& m_module;
        const TypeCheckerOptions& m_options;
        DiagnosticsBag& m_diagnostics;

        std::vector<CompilationUnit*> m_units;
        std::vector<NodeIndex> m_globalConstantDeclarations;
        std::vector<NodeIndex> m_functionDeclarations;
        std::vector<NodeIndex> m_typeDeclarations;
        std::vector<NodeIndex> m_enumDeclarations;

        std::vector<u32> m_pendingAnnotations;
        std::vector<u32> m_childrenScratch;
        std::vector<u32> m_childrenStack;
        std::vector<u32> m_annotationScratch;
        std::vector<u32> m_callArgumentStack;
        std::vector<u32> m_orderedSlotStack;

        Type m_defaultIntegerType = Type::Undefined();
        Type m_defaultFloatingType = Type::Undefined();
        Type m_defaultEnumBaseType = Type::Undefined();
        Type m_currentReturnType = Type::Void();
        Type m_currentType = Type::Undefined();
        bool m_leadingDotRootPending = false;
        std::string m_currentFunctionName;

        static constexpr u32 InvalidNameId = 0xFFFFFFFF;

        std::unordered_map<std::string_view, u32, TransparentStringHash, std::equal_to<>> m_nameIds;
        std::vector<std::string_view> m_nameLexemes;
        std::vector<std::vector<u32>> m_tokenNameIds;
        std::vector<std::vector<LexicalBinding>> m_bindingsByName;
        std::vector<u32> m_boundNames;
        std::vector<ScopeFrame> m_scopeFrames;
        std::unordered_map<std::string, InitConstantRecord> m_initConstants;
        std::unordered_map<std::string, SourceLocation> m_initConstantAssignments;
    };
}
