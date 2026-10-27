#pragma once

#include <Caracal/API.h>
#include <Caracal/Diagnostics/ArgumentTypeMismatchInfo.h>
#include <Caracal/Diagnostics/Diagnostic.h>
#include <Caracal/Semantic/AnnotationKind.h>
#include <Caracal/Syntax/TokenKind.h>

#include <optional>
#include <vector>

namespace Caracal
{
    class CARACAL_API DiagnosticsBag
    {
    public:
        DiagnosticsBag() = default;

        // Lexer diagnostics
        void addIllegalCharacterError(const SourceTextSharedPtr& source, const SourceLocation& location);
        void addUnterminatedStringError(const SourceTextSharedPtr& source, const SourceLocation& location);
        void addInvalidUtf8SourceError(const SourceTextSharedPtr& source, const SourceLocation& location, u8 byteValue);
        void addUnsupportedSourceEncodingError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& encodingName);

        // Parser syntax diagnostics
        void addExpectedTokenError(const SourceTextSharedPtr& source, const SourceLocation& location, TokenKind expectedKind, TokenKind actualKind);
        void addExpectedTokenError(const SourceTextSharedPtr& source, const SourceLocation& location, TokenKind expectedKind1, TokenKind expectedKind2, TokenKind actualKind);
        void addExtraTokensRemainingError(const SourceTextSharedPtr& source, const SourceLocation& location);
        void addUnexpectedTopLevelTokenError(const SourceTextSharedPtr& source, const SourceLocation& location, TokenKind actualKind);
        void addExpectedEnumFieldError(const SourceTextSharedPtr& source, const SourceLocation& location, TokenKind actualKind);
        void addUninitializedTypeFieldError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& fieldName);
        void addUnexpectedStatementTokenError(const SourceTextSharedPtr& source, const SourceLocation& location, TokenKind actualKind);
        void addUnexpectedExpressionTokenError(const SourceTextSharedPtr& source, const SourceLocation& location, TokenKind actualKind);
        void addUnexpectedParameterTokenError(const SourceTextSharedPtr& source, const SourceLocation& location, TokenKind actualKind);
        void addPositionalArgumentAfterNamedError(const SourceTextSharedPtr& source, const SourceLocation& location);
        void addAnnotationNotAllowedHereError(const SourceTextSharedPtr& source, const SourceLocation& location);
        void addExplicitTypeOnInitConstantError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& constantName, const std::string& initTypeName);
        void addPrivateStaticMethodError(const SourceTextSharedPtr& source, const SourceLocation& location);
        void addPrivateFreeFunctionError(const SourceTextSharedPtr& source, const SourceLocation& location);
        void addExpectedArrayLengthError(const SourceTextSharedPtr& source, const SourceLocation& location);
        void addReservedOperatorError(const SourceTextSharedPtr& source, const SourceLocation& location, TokenKind reservedOperator);

        // Annotation diagnostics
        void addDanglingAnnotationError(const SourceTextSharedPtr& source, const SourceLocation& location);
        void addUnknownAnnotationError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& annotationName, TokenKind targetKind);
        void addUnexpectedAnnotationTargetError(const SourceTextSharedPtr& source, const SourceLocation& location);
        void addAnnotationMissingArgumentsError(const SourceTextSharedPtr& source, const SourceLocation& location, AnnotationKind annotationKind, const std::string& annotationName, const std::string& argumentName = "");
        void addAnnotationWrongNumberOfArgumentsError(const SourceTextSharedPtr& source, const SourceLocation& location, AnnotationKind annotationKind, const std::string& annotationName, i32 expectedCount, i32 actualCount);
        void addAnnotationArgumentTypeMismatchError(const SourceTextSharedPtr& source, const SourceLocation& location, AnnotationKind annotationKind, const std::string& annotationName, const std::string& expectedDescription, const std::string& actualDescription);
        void addUnexpectedAnnotationArgumentError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& annotationName, const std::string& argumentName);
        void addUnsupportedAnnotationArgumentError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& argumentName, const std::string& kindName);
        void addDuplicateAnnotationArgumentError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& annotationName, const std::string& argumentName);
        void addExternMethodRequiresSymbolError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& methodName);
        void addConflictingEnumAnnotationsError(const SourceTextSharedPtr& source, const SourceLocation& location, const SourceLocation& otherLocation, const std::string& annotationName, const std::string& otherAnnotationName);

        // Unknown symbol and member access diagnostics
        void addUnknownNameError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& name);
        void addUnknownFunctionError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& functionName);
        void addUnknownTypeError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& typeName);
        void addUnknownMethodError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& receiverTypeName, const std::string& methodName);
        void addUnknownFieldError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& receiverTypeName, const std::string& fieldName);
        void addUnknownEnumFieldError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& enumName, const std::string& fieldName);
        void addInvalidEnumMemberAccessError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& enumName);
        void addInvalidMemberAccessReceiverError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& receiverTypeName);

        // Call diagnostics
        void addArgumentCountMismatchError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& functionName, i32 expectedCount, i32 actualCount, bool isVariadic);
        void addArgumentTypeMismatchError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& functionName, const std::vector<ArgumentTypeMismatchInfo>& mismatches);
        void addInvalidVariadicArgumentTypeError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& functionName, i32 argumentIndex, const std::string& actualTypeName);
        void addUnknownArgumentNameError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& functionName, const std::string& argumentName);
        void addDuplicateArgumentBindingError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& functionName, const std::string& parameterName);
        void addMissingRequiredArgumentError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& functionName, const std::string& parameterName);
        void addDefaultParameterTypeMismatchError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& expectedTypeName, const std::string& actualTypeName);
        void addNonTrailingDefaultParameterError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& parameterName);

        // init (write-once) constant diagnostics
        void addAssignmentToInitConstantError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& constantName);
        void addInitConstantAlreadyInitializedError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& constantName, const std::optional<SourceLocation>& previousLocation);
        void addUninitializedInitConstantError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& constantName);
        void addNonGlobalInitConstantError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& constantName);
        void addInitReferenceWithoutRefError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& constantName);
        void addAssignmentToConstantError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& constantName);
        void addAssignmentThroughConstantReferenceError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& referenceName);
        void addMutatingMethodOnConstantError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& constantName, const std::string& methodName);
        void addMutatingMethodThroughConstantReferenceError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& referenceName, const std::string& methodName);
        void addUnreachableCodeWarning(const SourceTextSharedPtr& source, const SourceLocation& location);
        void addMissingReturnError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& functionName, const std::string& returnTypeName);
        void addDivisionByZeroError(const SourceTextSharedPtr& source, const SourceLocation& location);
        void addConstantOverflowError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& typeName);
        void addUnaryOperandTypeMismatchError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& operatorName, const std::string& operandTypeName, const std::string& expectedDescription);
        void addVoidParameterTypeError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& parameterName);
        void addBinaryOperandTypeMismatchError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& operatorName, const std::string& operandTypeName, const std::string& expectedDescription);

        // #builtin annotation diagnostics
        void addNotABuiltinTypeError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& typeName);
        void addDuplicateBuiltinTypeBindingError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& typeName, const std::optional<SourceLocation>& otherLocation);
        void addBuiltinTypeFieldIgnoredWarning(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& fieldName);
        void addBuiltinTypeConstructorIgnoredWarning(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& typeName);
        void addBuiltinMethodBodyIgnoredWarning(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& methodName);
        void addUnknownBuiltinMethodIgnoredWarning(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& methodName, const std::string& typeName);
        void addInvalidOperatorMethodSignatureError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& methodName, const std::string& expectedSignature);
        void addBitwiseMethodOnNonIntegerTypeError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& methodName, const std::string& typeName);
        void addUnsupportedFloatBitsError(const SourceTextSharedPtr& source, const SourceLocation& location);
        void addMethodCallMissingDotError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& methodName, const std::string& typeName);
        void addMemberAccessInDefaultParameterError(const SourceTextSharedPtr& source, const SourceLocation& location);
        void addPrivateMethodCallOutsideTypeError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& methodName, const std::string& typeName);
        void addStaticMethodTypeNameMismatchError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& declaredTypeName, const std::string& enclosingTypeName, const std::string& methodName);

        // Control flow diagnostics
        void addNonBoolIfConditionError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& actualTypeName);
        void addNonBoolWhileConditionError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& actualTypeName);

        // Type mismatch diagnostics
        void addReturnTypeMismatchError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& expectedTypeName, const std::string& actualTypeName);
        void addAssignmentTypeMismatchError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& expectedTypeName, const std::string& actualTypeName);
        void addExplicitConstantTypeMismatchError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& expectedTypeName, const std::string& actualTypeName);
        void addExplicitVariableTypeMismatchError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& expectedTypeName, const std::string& actualTypeName);
        void addTypeFieldInitializerMismatchError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& expectedTypeName, const std::string& actualTypeName);
        void addArithmeticOperandTypeMismatchError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& operatorName, const std::string& leftTypeName, const std::string& rightTypeName);
        void addComparisonOperandTypeMismatchError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& operatorName, const std::string& leftTypeName, const std::string& rightTypeName);
        void addEnumFieldValueTypeMismatchError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& expectedTypeName, const std::string& actualTypeName);
        void addArrayElementTypeMismatchError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& expectedTypeName, const std::string& actualTypeName);
        void addArrayLengthMismatchError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& expectedTypeName, i32 expectedLength, i32 actualLength);

        // Declaration-shape and reference diagnostics
        void addNonExternVariadicFunctionError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& functionName);
        void addFlagEnumExplicitValueError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& fieldName);
        void addTypeDotNewDeclarationError(const SourceTextSharedPtr& source, const SourceLocation& location, const SourceLocation& otherLocation, const std::string& typeName);
        void addReferenceOrSliceReturnError(const SourceTextSharedPtr& source, const SourceLocation& location);
        void addReferenceOrSliceFieldError(const SourceTextSharedPtr& source, const SourceLocation& location);
        void addDynamicArrayElementError(const SourceTextSharedPtr& source, const SourceLocation& location);
        void addReferenceToNonVariableError(const SourceTextSharedPtr& source, const SourceLocation& location);
        void addMemberAccessInStaticMethodError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& methodName);
        void addPrivateFieldAccessOutsideTypeError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& fieldName, const std::string& typeName);
        void addMutatingMethodOnImmutableSliceError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& methodName);
        void addFieldAccessMissingDotError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& fieldName);
        void addMethodRequiresDynamicArrayError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& receiverTypeName, const std::string& dynamicTypeName);
        void addReferenceOrSliceArrayElementError(const SourceTextSharedPtr& source, const SourceLocation& location);
        void addAlreadyReferenceError(const SourceTextSharedPtr& source, const SourceLocation& location);
        void addCallReturnsNoValueError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& name);
        void addGlobalConstantWithCallError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& name);
        void addGlobalConstantNotComputableError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& name);

        // Duplicate declaration diagnostics
        void addDuplicateDeclarationError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& name, const SourceTextSharedPtr& otherSource = nullptr, std::optional<SourceLocation> otherLocation = std::nullopt);
        void addDuplicateConstantDeclarationError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& name, const SourceTextSharedPtr& otherSource = nullptr, std::optional<SourceLocation> otherLocation = std::nullopt);
        void addDuplicateVariableDeclarationError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& name, const SourceTextSharedPtr& otherSource = nullptr, std::optional<SourceLocation> otherLocation = std::nullopt);
        void addDuplicateParameterDeclarationError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& name, const SourceTextSharedPtr& otherSource = nullptr, std::optional<SourceLocation> otherLocation = std::nullopt);
        void addDuplicateEnumFieldDeclarationError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& name, std::optional<SourceLocation> otherLocation = std::nullopt);
        void addDuplicateTypeFieldDeclarationError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& name, std::optional<SourceLocation> otherLocation = std::nullopt);
        void addDuplicateFunctionDeclarationError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& functionName, const SourceTextSharedPtr& otherSource = nullptr, std::optional<SourceLocation> otherLocation = std::nullopt);
        void addDuplicateTypeDeclarationError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& typeName, const SourceTextSharedPtr& otherSource = nullptr, std::optional<SourceLocation> otherLocation = std::nullopt);
        void addDuplicateMethodDeclarationError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& methodName, const std::string& typeName, std::optional<SourceLocation> otherLocation = std::nullopt);

        // Literal diagnostics
        void addNumberLiteralOutOfRangeError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& literalText, const std::string& targetTypeName);
        void addEmptyArrayLiteralError(const SourceTextSharedPtr& source, const SourceLocation& location);

        // Warning diagnostics
        void addUnusedLocalVariableWarning(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& name);
        void addUnusedParameterWarning(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& name);
        void addUnusedLocalConstantWarning(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& name);

        // Prelude diagnostics
        void addPreludeTypeMissingError(const std::string& typeName);
        void addPreludeCFunctionMissingError(const std::string& symbolName, const std::string& signature);
        void addPreludeCFunctionMismatchError(const std::string& functionName, const std::string& signature);
        void addPreludeCFunctionWithoutRequiredError(const std::string& functionName);

        const std::vector<Diagnostic>& diagnostics() const;
        [[nodiscard]] bool hasErrors() const noexcept;
        [[nodiscard]] bool hasWarnings() const noexcept;
        [[nodiscard]] int errorCount() const noexcept;

    private:
        std::vector<Diagnostic> m_diagnostics;
    };
}
