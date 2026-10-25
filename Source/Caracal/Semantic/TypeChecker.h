#pragma once

#include <Caracal/API.h>
#include <Caracal/CompilationContext.h>
#include <Caracal/IR/IREmitter.h>
#include <Caracal/Optimization/OperatorFolding.h>
#include <Caracal/Defines.h>
#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/IR/ValueRef.h>
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
#include <unordered_set>
#include <vector>

namespace Caracal
{
    class CARACAL_API TypeChecker
    {
    public:
        TypeChecker(
            CompilationContext& compilationContext,
            bool isPreludePass,
            DiagnosticsBag& diagnostics,
            Module& irModule);

        CARACAL_DELETE_COPY_DELETE_MOVE(TypeChecker)

        [[nodiscard]] bool typeCheck();

    private:
        struct ExpressionContext
        {
            std::optional<Type> contextualNumberType;
            bool negatedLiteral = false;
            bool wantsAddress = false;
            bool receiverPosition = false;
        };

        struct CheckedValue
        {
            Type type;
            ValueRef value{};
            std::optional<FoldValue> folded{};
            bool signFolded = false;

            CheckedValue(Type checkedType)
                : type{ checkedType }
            {
            }
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
            std::optional<FoldValue> folded{};
        };

        struct ScopeFrame
        {
            u32 boundNamesStart = 0;
            ScopeKind kind = ScopeKind::Invalid;
        };
        
        struct CapturedArrayElement
        {
            u32 node;
            ValueRef value;
            std::optional<FoldValue> folded;
        };

        struct ConstructorDestination
        {
            ValueRef address;
            bool loadResult = false;
        };

        void collectDeclarations();
        void collectUnitDeclarations(CompilationUnit& unit);
        void collectFunctionDeclaration(const CompilationUnit& unit, u32 index);
        void collectEnumDeclaration(const CompilationUnit& unit, u32 index);
        void collectTypeDeclaration(const CompilationUnit& unit, u32 index);
        void collectBuiltinTypeDeclaration(const CompilationUnit& unit, u32 index);
        void collectMethodDeclarations();
        void collectMethodAnnotationsBefore(const CompilationUnit& unit, u32 methodIndex, const TypeDefinition& typeDefinition, std::vector<u32>& annotations) const;
        [[nodiscard]] const std::vector<u32>& collectAddressTakenNameIds(const CompilationUnit& unit, u32 bodyIndex);
        [[nodiscard]] std::vector<u32> collectRootStatements(const NodeBuffer& buffer) const;
        [[nodiscard]] std::vector<Parameter> collectParameterNames(const CompilationUnit& unit, u32 parametersNode);

        [[nodiscard]] BuiltinTypeDescription extractPendingBuiltinTypeDescription(const CompilationUnit& unit) const;
        void reportDuplicateTypeDeclaration(const CompilationUnit& unit, u32 index, std::string_view name, Type existingType);
        void resolveWellKnownTypes();
        void checkUninitializedInitConstants();
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
        void typeCheckStatementNode(const CompilationUnit& unit, u32 index);
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

        [[nodiscard]] CheckedValue typeCheckExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context);
        [[nodiscard]] CheckedValue typeCheckGroupingExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context);
        [[nodiscard]] CheckedValue typeCheckUnaryExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context);
        [[nodiscard]] CheckedValue typeCheckBinaryExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context);
        [[nodiscard]] CheckedValue typeCheckNameExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context);
        [[nodiscard]] CheckedValue typeCheckFunctionCallExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context);
        [[nodiscard]] CheckedValue typeCheckResolvedFunctionCall(const CompilationUnit& unit, u32 index, Type functionType, const ExpressionContext& context);
        [[nodiscard]] CheckedValue typeCheckMemberAccessExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context);
        [[nodiscard]] CheckedValue finishResolvedMethodCall(const CompilationUnit& unit, u32 index, u32 callIndex, u32 nameExpressionIndex, Type methodType, const ExpressionContext& context, std::optional<ValueRef> implicitArgument = std::nullopt);
        [[nodiscard]] bool typeCheckCallArguments(const CompilationUnit& unit, u32 callIndex, Type functionType, const ExpressionContext& context, ValueRef& emittedCallValue, std::optional<FoldValue>& emittedCallFold, std::optional<ValueRef> implicitArgument = std::nullopt);

        [[nodiscard]] Type tryResolveConstructedType(const CompilationUnit& unit, u32 strippedIndex);
        [[nodiscard]] ValueRef memberReceiverAddress(const CompilationUnit& unit, u32 leftIndex);
        [[nodiscard]] ValueRef addressOfExpression(const CompilationUnit& unit, u32 index);
        [[nodiscard]] Type resolveAggregateInitializerType(const CompilationUnit& unit, Type declaredType, u32 strippedInitializer, Type constructedType, bool& initializerWantsAddress);
        void emitLocalBinding(const CompilationUnit& unit, u32 nameId, std::string_view name, Type bindingType, Type rightType, u32 strippedRight, const CheckedValue& checkedRight, bool referencesConstant, ValueRef aggregateDestination, bool constructorFilled);
        [[nodiscard]] bool memberLeftYieldsAddress(const CompilationUnit& unit, u32 leftIndex);
        void recordExpressionEnumFieldConstant(const CompilationUnit& unit, Type enumType, std::string_view fieldName, u32 expressionIndex, const CheckedValue& checkedFieldValue);
        [[nodiscard]] std::string escapedStringContent(const CompilationUnit& unit, u32 literalIndex);
        void reportBinaryFoldFailure(const CompilationUnit& unit, FoldResultKind kind, Type operandType, u32 index, u32 rightIndex);
        void reportUnaryFoldFailure(const CompilationUnit& unit, FoldResultKind kind, Type operandType, u32 index);
        void emitCapturedArrayLiteral(const CompilationUnit& unit, u32 literalIndex, ValueRef destination, Type arrayType);
        [[nodiscard]] std::optional<ConstantValue> tryBuildAggregateConstant(const CompilationUnit& unit, u32 literalIndex);
        [[nodiscard]] CheckedValue typeCheckNumberLiteral(const CompilationUnit& unit, u32 index, const ExpressionContext& context);
        [[nodiscard]] CheckedValue typeCheckArrayLiteral(const CompilationUnit& unit, u32 index, const ExpressionContext& context);
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

        [[nodiscard]] Token tokenAt(u16 fileId, u32 tokenIndex) const noexcept;
        [[nodiscard]] std::string_view nameOf(const CompilationUnit& unit, u32 index) const noexcept;
        [[nodiscard]] const SourceLocation& nameLocation(const CompilationUnit& unit, u32 index) const noexcept;
        [[nodiscard]] SourceLine tokenLine(const CompilationUnit& unit, u32 tokenIndex) const;
        [[nodiscard]] SourceLine nameLine(const CompilationUnit& unit, u32 index) const;
        [[nodiscard]] SourceLine statementLine(const CompilationUnit& unit, u32 index) const;
        [[nodiscard]] SourceLocation annotationLocation(const CompilationUnit& unit, u32 index) const noexcept;
        void locateDefinitionName(NodeIndex definitionNode, SourceTextSharedPtr& source, std::optional<SourceLocation>& location) const noexcept;
        [[nodiscard]] bool pendingAnnotationsContain(const CompilationUnit& unit, std::string_view name) const noexcept;

        Type recordType(const CompilationUnit& unit, u32 index, Type type) noexcept
        {
            m_module.setNodeType(NodeIndex{ unit.fileId, index }, type);
            return type;
        }

        [[nodiscard]] CompilationUnit& unitFor(NodeIndex index) const noexcept;
        [[nodiscard]] ValueRef emitDefaultArgumentExpression(NodeIndex defaultNode, Type parameterType);
        [[nodiscard]] ValueRef materializeValue(const CheckedValue& checked);
        [[nodiscard]] ValueRef dynamicArraySourceAddress(const CompilationUnit& unit, u32 nameNode, Type arrayType);

        CompilationContext& m_compilationContext;
        bool m_isPreludePass;
        SemanticContext& m_module;
        IREmitter m_emitter;
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
        std::vector<ValueRef> m_callArgumentValues;
        std::vector<u32> m_addressTakenScratch;

        Type m_defaultIntegerType = Type::Undefined();
        Type m_defaultFloatingType = Type::Undefined();
        Type m_defaultEnumBaseType = Type::Undefined();
        Type m_currentReturnType = Type::Void();
        Type m_currentType = Type::Undefined();
        bool m_leadingDotRootPending = false;
        std::string m_currentFunctionName;

        static constexpr u32 InvalidNameId = 0xFFFFFFFF;
        u32 m_thisNameId = 0;
        bool m_sawUnfoldedBitwiseCall = false;

        std::unordered_map<std::string_view, u32, TransparentStringHash, std::equal_to<>> m_nameIds;
        std::vector<std::string_view> m_nameLexemes;
        std::vector<std::vector<u32>> m_tokenNameIds;
        std::vector<std::vector<LexicalBinding>> m_bindingsByName;
        std::vector<ConstructorDestination> m_constructorDestinations;
        std::unordered_map<u64, std::vector<CapturedArrayElement>> m_arrayLiteralElements;
        std::vector<u32> m_boundNames;
        std::vector<ScopeFrame> m_scopeFrames;
        std::unordered_map<std::string, InitConstantRecord> m_initConstants;
        std::unordered_map<std::string, SourceLocation> m_initConstantAssignments;
    };
}
