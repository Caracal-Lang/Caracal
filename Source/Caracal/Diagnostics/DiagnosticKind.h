#pragma once

namespace Caracal
{
    enum class DiagnosticKind
    {
        Unknown,

        // Lexer diagnostics
        L0001_IllegalCharacter,
        L0002_UnterminatedString,
        L0003_InvalidUtf8Source,
        L0004_UnsupportedSourceEncoding,

        // Parser syntax diagnostics
        P0001_UnexpectedToken,
        P0002_UnexpectedTrailingTokens,
        P0003_UnexpectedTopLevelToken,
        P0004_InvalidEnumField,
        P0005_InvalidStatement,
        P0006_InvalidExpression,
        P0007_DanglingAnnotation,
        P0008_UninitializedField,
        P0009_PositionalArgumentAfterNamed,
        P0010_AnnotationNotAllowedHere,
        P0011_InvalidParameter,
        P0012_ExplicitTypeOnInitConstant,
        P0013_PrivateStaticMethod,
        P0014_PrivateFreeFunction,
        P0015_ExpectedArrayLength,
        P0016_ReservedOperator,

        // Annotation diagnostics
        T0001_UnknownAnnotation,
        T0002_UnexpectedAnnotationTarget,
        T0003_MissingAnnotationArguments,
        T0004_WrongNumberOfAnnotationArguments,
        T0005_AnnotationArgumentTypeMismatch,
        T0006_ConflictingEnumAnnotations,

        // Unknown symbol and member access diagnostics
        T0007_UnknownName,
        T0008_UnknownFunction,
        T0009_UnknownType,
        T0010_UnknownMethod,
        T0011_UnknownField,
        T0012_UnknownEnumField,
        T0013_InvalidEnumMemberAccess,
        T0014_InvalidMemberAccessReceiver,
        T0073_MethodCallMissingDot,
        T0076_MemberAccessInDefaultParameter,
        T0077_PrivateMethodCallOutsideType,
        T0093_MemberAccessInStaticMethod,
        T0094_PrivateFieldAccessOutsideType,
        T0095_MutatingMethodOnImmutableSlice,
        T0096_FieldAccessMissingDot,

        // Call diagnostics
        T0015_ArgumentCountMismatch,
        T0016_ArgumentTypeMismatch,
        T0017_InvalidVariadicArgumentType,

        // Control flow diagnostics
        T0018_NonBoolIfCondition,
        T0019_NonBoolWhileCondition,

        // Type mismatch diagnostics
        T0020_ReturnTypeMismatch,
        T0021_AssignmentTypeMismatch,
        T0022_ExplicitConstantTypeMismatch,
        T0023_ExplicitVariableTypeMismatch,
        T0024_TypeFieldInitializerMismatch,
        T0025_ArithmeticOperandTypeMismatch,
        T0026_ComparisonOperandTypeMismatch,
        T0027_EnumFieldValueTypeMismatch,
        T0078_ArrayElementTypeMismatch,
        T0080_ArrayLengthMismatch,

        // Declaration-shape and reference diagnostics
        T0028_NonExternVariadicFunction,
        T0029_FlagEnumExplicitValue,
        T0030_TypeDotNewDeclaration,
        T0031_ReferenceOrSliceReturn,
        T0032_AlreadyReference,
        T0074_StaticMethodTypeNameMismatch,
        T0081_CallReturnsNoValue,
        T0082_GlobalConstantWithCall,
        T0084_GlobalConstantNotComputable,
        T0085_ReferenceOrSliceField,
        T0086_ReferenceOrSliceArrayElement,
        T0090_MethodRequiresDynamicArray,
        T0091_DynamicArrayElement,
        T0092_ReferenceToNonVariable,

        // Duplicate declaration diagnostics
        T0033_DuplicateDeclaration,
        T0034_DuplicateConstantDeclaration,
        T0035_DuplicateVariableDeclaration,
        T0036_DuplicateParameterDeclaration,
        T0037_DuplicateEnumFieldDeclaration,
        T0038_DuplicateTypeFieldDeclaration,
        T0039_DuplicateFunctionDeclaration,
        T0040_DuplicateTypeDeclaration,
        T0075_DuplicateMethodDeclaration,

        // Literal diagnostics
        T0041_NumberLiteralOutOfRange,
        T0079_EmptyArrayLiteral,

        // Warning diagnostics
        T0042_UnusedLocalVariable,
        T0043_UnusedLocalConstant,
        T0044_UnusedParameter,

        // Annotation argument diagnostics
        T0045_UnexpectedAnnotationArgument,
        T0046_DuplicateAnnotationArgument,

        // Extern diagnostics
        T0047_ExternMethodRequiresSymbol,

        // Call argument binding diagnostics
        T0048_UnknownArgumentName,
        T0049_DuplicateArgumentBinding,
        T0050_MissingRequiredArgument,

        // Default parameter diagnostics
        T0051_DefaultParameterTypeMismatch,
        T0052_NonTrailingDefaultParameter,

        // init (write-once) constant diagnostics
        T0053_AssignmentToInitConstant,
        T0054_InitConstantAlreadyInitialized,
        T0055_UninitializedInitConstant,
        T0056_NonGlobalInitConstant,
        T0100_InitReferenceWithoutRef,

        // Constness diagnostics
        T0057_AssignmentToConstant,
        T0058_AssignmentThroughConstantReference,
        T0087_MutatingMethodOnConstant,
        T0088_MutatingMethodThroughConstantReference,

        // Control-flow diagnostics
        T0059_UnreachableCode,
        T0060_MissingReturn,

        // Arithmetic diagnostics
        T0061_DivisionByZero,
        T0062_ConstantOverflow,
        T0063_UnaryOperandTypeMismatch,
        T0064_VoidParameterType,
        T0065_BinaryOperandTypeMismatch,

        // #builtin annotation diagnostics
        T0066_NotABuiltinType,
        T0067_DuplicateBuiltinTypeBinding,
        T0068_BuiltinTypeFieldIgnored,
        T0069_BuiltinTypeConstructorIgnored,
        T0070_BuiltinMethodBodyIgnored,
        T0071_UnknownBuiltinMethodIgnored,
        T0072_InvalidOperatorMethodSignature,
        T0083_BitwiseMethodOnNonIntegerType,
        T0089_UnsupportedFloatBits,

        // Prelude diagnostics
        T0097_PreludeTypeMissing,
        T0098_PreludeCFunctionMissing,
        T0099_PreludeCFunctionMismatch,
    };
}
