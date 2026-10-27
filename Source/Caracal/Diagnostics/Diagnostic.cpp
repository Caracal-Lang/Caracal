#include <Caracal/Diagnostics/Diagnostic.h>

namespace Caracal
{
    static std::string_view DiagnosticCode(DiagnosticKind kind)
    {
        switch (kind)
        {
            // Lexer diagnostics
            case DiagnosticKind::L0001_IllegalCharacter:
                return "L0001";
            case DiagnosticKind::L0002_UnterminatedString:
                return "L0002";
            case DiagnosticKind::L0003_InvalidUtf8Source:
                return "L0003";
            case DiagnosticKind::L0004_UnsupportedSourceEncoding:
                return "L0004";

            // Parser syntax diagnostics
            case DiagnosticKind::P0001_UnexpectedToken:
                return "P0001";
            case DiagnosticKind::P0002_UnexpectedTrailingTokens:
                return "P0002";
            case DiagnosticKind::P0003_UnexpectedTopLevelToken:
                return "P0003";
            case DiagnosticKind::P0004_InvalidEnumField:
                return "P0004";
            case DiagnosticKind::P0005_InvalidStatement:
                return "P0005";
            case DiagnosticKind::P0006_InvalidExpression:
                return "P0006";
            case DiagnosticKind::P0007_DanglingAnnotation:
                return "P0007";
            case DiagnosticKind::P0008_UninitializedField:
                return "P0008";
            case DiagnosticKind::P0009_PositionalArgumentAfterNamed:
                return "P0009";
            case DiagnosticKind::P0010_AnnotationNotAllowedHere:
                return "P0010";
            case DiagnosticKind::P0011_InvalidParameter:
                return "P0011";
            case DiagnosticKind::P0012_ExplicitTypeOnInitConstant:
                return "P0012";
            case DiagnosticKind::P0013_PrivateStaticMethod:
                return "P0013";
            case DiagnosticKind::P0014_PrivateFreeFunction:
                return "P0014";
            case DiagnosticKind::P0015_ExpectedArrayLength:
                return "P0015";
            case DiagnosticKind::P0016_ReservedOperator:
                return "P0016";

            // Annotation diagnostics
            case DiagnosticKind::T0001_UnknownAnnotation:
                return "T0001";
            case DiagnosticKind::T0002_UnexpectedAnnotationTarget:
                return "T0002";
            case DiagnosticKind::T0003_MissingAnnotationArguments:
                return "T0003";
            case DiagnosticKind::T0004_WrongNumberOfAnnotationArguments:
                return "T0004";
            case DiagnosticKind::T0005_AnnotationArgumentTypeMismatch:
                return "T0005";
            case DiagnosticKind::T0006_ConflictingEnumAnnotations:
                return "T0006";

            // Unknown symbol and member access diagnostics
            case DiagnosticKind::T0007_UnknownName:
                return "T0007";
            case DiagnosticKind::T0008_UnknownFunction:
                return "T0008";
            case DiagnosticKind::T0009_UnknownType:
                return "T0009";
            case DiagnosticKind::T0010_UnknownMethod:
                return "T0010";
            case DiagnosticKind::T0011_UnknownField:
                return "T0011";
            case DiagnosticKind::T0012_UnknownEnumField:
                return "T0012";
            case DiagnosticKind::T0013_InvalidEnumMemberAccess:
                return "T0013";
            case DiagnosticKind::T0014_InvalidMemberAccessReceiver:
                return "T0014";

            // Call diagnostics
            case DiagnosticKind::T0015_ArgumentCountMismatch:
                return "T0015";
            case DiagnosticKind::T0016_ArgumentTypeMismatch:
                return "T0016";
            case DiagnosticKind::T0017_InvalidVariadicArgumentType:
                return "T0017";

            // Control flow diagnostics
            case DiagnosticKind::T0018_NonBoolIfCondition:
                return "T0018";
            case DiagnosticKind::T0019_NonBoolWhileCondition:
                return "T0019";

            // Type mismatch diagnostics
            case DiagnosticKind::T0020_ReturnTypeMismatch:
                return "T0020";
            case DiagnosticKind::T0021_AssignmentTypeMismatch:
                return "T0021";
            case DiagnosticKind::T0022_ExplicitConstantTypeMismatch:
                return "T0022";
            case DiagnosticKind::T0023_ExplicitVariableTypeMismatch:
                return "T0023";
            case DiagnosticKind::T0024_TypeFieldInitializerMismatch:
                return "T0024";
            case DiagnosticKind::T0025_ArithmeticOperandTypeMismatch:
                return "T0025";
            case DiagnosticKind::T0026_ComparisonOperandTypeMismatch:
                return "T0026";
            case DiagnosticKind::T0027_EnumFieldValueTypeMismatch:
                return "T0027";

            // Declaration-shape and reference diagnostics
            case DiagnosticKind::T0028_NonExternVariadicFunction:
                return "T0028";
            case DiagnosticKind::T0029_FlagEnumExplicitValue:
                return "T0029";
            case DiagnosticKind::T0030_TypeDotNewDeclaration:
                return "T0030";
            case DiagnosticKind::T0031_ReferenceOrSliceReturn:
                return "T0031";
            case DiagnosticKind::T0032_AlreadyReference:
                return "T0032";

            // Duplicate declaration diagnostics
            case DiagnosticKind::T0033_DuplicateDeclaration:
                return "T0033";
            case DiagnosticKind::T0034_DuplicateConstantDeclaration:
                return "T0034";
            case DiagnosticKind::T0035_DuplicateVariableDeclaration:
                return "T0035";
            case DiagnosticKind::T0036_DuplicateParameterDeclaration:
                return "T0036";
            case DiagnosticKind::T0037_DuplicateEnumFieldDeclaration:
                return "T0037";
            case DiagnosticKind::T0038_DuplicateTypeFieldDeclaration:
                return "T0038";
            case DiagnosticKind::T0039_DuplicateFunctionDeclaration:
                return "T0039";
            case DiagnosticKind::T0040_DuplicateTypeDeclaration:
                return "T0040";
            case DiagnosticKind::T0041_NumberLiteralOutOfRange:
                return "T0041";
            case DiagnosticKind::T0042_UnusedLocalVariable:
                return "T0042";
            case DiagnosticKind::T0043_UnusedLocalConstant:
                return "T0043";
            case DiagnosticKind::T0044_UnusedParameter:
                return "T0044";
            case DiagnosticKind::T0045_UnexpectedAnnotationArgument:
                return "T0045";
            case DiagnosticKind::T0046_DuplicateAnnotationArgument:
                return "T0046";
            case DiagnosticKind::T0047_ExternMethodRequiresSymbol:
                return "T0047";
            case DiagnosticKind::T0048_UnknownArgumentName:
                return "T0048";
            case DiagnosticKind::T0049_DuplicateArgumentBinding:
                return "T0049";
            case DiagnosticKind::T0050_MissingRequiredArgument:
                return "T0050";
            case DiagnosticKind::T0051_DefaultParameterTypeMismatch:
                return "T0051";
            case DiagnosticKind::T0052_NonTrailingDefaultParameter:
                return "T0052";
            case DiagnosticKind::T0053_AssignmentToInitConstant:
                return "T0053";
            case DiagnosticKind::T0054_InitConstantAlreadyInitialized:
                return "T0054";
            case DiagnosticKind::T0055_UninitializedInitConstant:
                return "T0055";
            case DiagnosticKind::T0056_NonGlobalInitConstant:
                return "T0056";
            case DiagnosticKind::T0100_InitReferenceWithoutRef:
                return "T0100";
            case DiagnosticKind::T0057_AssignmentToConstant:
                return "T0057";
            case DiagnosticKind::T0087_MutatingMethodOnConstant:
                return "T0087";
            case DiagnosticKind::T0088_MutatingMethodThroughConstantReference:
                return "T0088";
            case DiagnosticKind::T0058_AssignmentThroughConstantReference:
                return "T0058";
            case DiagnosticKind::T0059_UnreachableCode:
                return "T0059";
            case DiagnosticKind::T0060_MissingReturn:
                return "T0060";
            case DiagnosticKind::T0061_DivisionByZero:
                return "T0061";
            case DiagnosticKind::T0062_ConstantOverflow:
                return "T0062";
            case DiagnosticKind::T0063_UnaryOperandTypeMismatch:
                return "T0063";
            case DiagnosticKind::T0064_VoidParameterType:
                return "T0064";
            case DiagnosticKind::T0065_BinaryOperandTypeMismatch:
                return "T0065";
            case DiagnosticKind::T0066_NotABuiltinType:
                return "T0066";
            case DiagnosticKind::T0067_DuplicateBuiltinTypeBinding:
                return "T0067";
            case DiagnosticKind::T0068_BuiltinTypeFieldIgnored:
                return "T0068";
            case DiagnosticKind::T0069_BuiltinTypeConstructorIgnored:
                return "T0069";
            case DiagnosticKind::T0070_BuiltinMethodBodyIgnored:
                return "T0070";
            case DiagnosticKind::T0071_UnknownBuiltinMethodIgnored:
                return "T0071";
            case DiagnosticKind::T0072_InvalidOperatorMethodSignature:
                return "T0072";
            case DiagnosticKind::T0073_MethodCallMissingDot:
                return "T0073";
            case DiagnosticKind::T0074_StaticMethodTypeNameMismatch:
                return "T0074";
            case DiagnosticKind::T0075_DuplicateMethodDeclaration:
                return "T0075";
            case DiagnosticKind::T0076_MemberAccessInDefaultParameter:
                return "T0076";
            case DiagnosticKind::T0077_PrivateMethodCallOutsideType:
                return "T0077";
            case DiagnosticKind::T0078_ArrayElementTypeMismatch:
                return "T0078";
            case DiagnosticKind::T0079_EmptyArrayLiteral:
                return "T0079";
            case DiagnosticKind::T0080_ArrayLengthMismatch:
                return "T0080";
            case DiagnosticKind::T0081_CallReturnsNoValue:
                return "T0081";
            case DiagnosticKind::T0082_GlobalConstantWithCall:
                return "T0082";
            case DiagnosticKind::T0083_BitwiseMethodOnNonIntegerType:
                return "T0083";
            case DiagnosticKind::T0089_UnsupportedFloatBits:
                return "T0089";
            case DiagnosticKind::T0084_GlobalConstantNotComputable:
                return "T0084";
            case DiagnosticKind::T0085_ReferenceOrSliceField:
                return "T0085";
            case DiagnosticKind::T0086_ReferenceOrSliceArrayElement:
                return "T0086";
            case DiagnosticKind::T0090_MethodRequiresDynamicArray:
                return "T0090";
            case DiagnosticKind::T0091_DynamicArrayElement:
                return "T0091";
            case DiagnosticKind::T0092_ReferenceToNonVariable:
                return "T0092";
            case DiagnosticKind::T0093_MemberAccessInStaticMethod:
                return "T0093";
            case DiagnosticKind::T0094_PrivateFieldAccessOutsideType:
                return "T0094";
            case DiagnosticKind::T0095_MutatingMethodOnImmutableSlice:
                return "T0095";
            case DiagnosticKind::T0096_FieldAccessMissingDot:
                return "T0096";
            case DiagnosticKind::T0097_PreludeTypeMissing:
                return "T0097";
            case DiagnosticKind::T0098_PreludeCFunctionMissing:
                return "T0098";
            case DiagnosticKind::T0099_PreludeCFunctionMismatch:
                return "T0099";
            case DiagnosticKind::Unknown:
            default:
                return "?????";
        }
    }

    static std::string DiagnosticMessage(DiagnosticKind kind)
    {
        switch (kind)
        {
            // Lexer diagnostics
            case DiagnosticKind::L0001_IllegalCharacter:
                return "Illegal character";
            case DiagnosticKind::L0002_UnterminatedString:
                return "Unterminated string";
            case DiagnosticKind::L0003_InvalidUtf8Source:
                return "Invalid UTF-8 in source file";
            case DiagnosticKind::L0004_UnsupportedSourceEncoding:
                return "Unsupported source file encoding";

            // Parser syntax diagnostics
            case DiagnosticKind::P0001_UnexpectedToken:
                return "Unexpected token";
            case DiagnosticKind::P0002_UnexpectedTrailingTokens:
                return "Unexpected trailing tokens";
            case DiagnosticKind::P0003_UnexpectedTopLevelToken:
                return "Unexpected top-level token";
            case DiagnosticKind::P0004_InvalidEnumField:
                return "Invalid enum field";
            case DiagnosticKind::P0005_InvalidStatement:
                return "Invalid statement";
            case DiagnosticKind::P0006_InvalidExpression:
                return "Invalid expression";
            case DiagnosticKind::P0007_DanglingAnnotation:
                return "Dangling annotation";
            case DiagnosticKind::P0008_UninitializedField:
                return "Uninitialized field";
            case DiagnosticKind::P0009_PositionalArgumentAfterNamed:
                return "Positional argument after named argument";
            case DiagnosticKind::P0010_AnnotationNotAllowedHere:
                return "Annotation not allowed here";
            case DiagnosticKind::P0011_InvalidParameter:
                return "Invalid parameter";
            case DiagnosticKind::P0012_ExplicitTypeOnInitConstant:
                return "Explicit type is not allowed for init constants";
            case DiagnosticKind::P0013_PrivateStaticMethod:
                return "Static methods cannot be private";
            case DiagnosticKind::P0014_PrivateFreeFunction:
                return "Free functions cannot be private";
            case DiagnosticKind::P0015_ExpectedArrayLength:
                return "Expected an array length";
            case DiagnosticKind::P0016_ReservedOperator:
                return "Plain arithmetic operators are reserved until checked arithmetic exists";

            // Annotation diagnostics
            case DiagnosticKind::T0001_UnknownAnnotation:
                return "Unknown annotation";
            case DiagnosticKind::T0002_UnexpectedAnnotationTarget:
                return "Unexpected annotation target";
            case DiagnosticKind::T0003_MissingAnnotationArguments:
                return "Missing annotation arguments";
            case DiagnosticKind::T0004_WrongNumberOfAnnotationArguments:
                return "Wrong number of annotation arguments";
            case DiagnosticKind::T0005_AnnotationArgumentTypeMismatch:
                return "Annotation argument type mismatch";
            case DiagnosticKind::T0006_ConflictingEnumAnnotations:
                return "Conflicting enum annotations";

            // Unknown symbol and member access diagnostics
            case DiagnosticKind::T0007_UnknownName:
                return "Unknown name";
            case DiagnosticKind::T0008_UnknownFunction:
                return "Unknown function";
            case DiagnosticKind::T0009_UnknownType:
                return "Unknown type";
            case DiagnosticKind::T0010_UnknownMethod:
                return "Unknown method";
            case DiagnosticKind::T0011_UnknownField:
                return "Unknown field";
            case DiagnosticKind::T0012_UnknownEnumField:
                return "Unknown enum field";
            case DiagnosticKind::T0013_InvalidEnumMemberAccess:
                return "Invalid enum member access";
            case DiagnosticKind::T0014_InvalidMemberAccessReceiver:
                return "Invalid member access receiver";

            // Call diagnostics
            case DiagnosticKind::T0015_ArgumentCountMismatch:
                return "Argument count mismatch";
            case DiagnosticKind::T0016_ArgumentTypeMismatch:
                return "Argument type mismatch";
            case DiagnosticKind::T0017_InvalidVariadicArgumentType:
                return "Invalid variadic argument type";

            // Control flow diagnostics
            case DiagnosticKind::T0018_NonBoolIfCondition:
                return "Non-bool if condition";
            case DiagnosticKind::T0019_NonBoolWhileCondition:
                return "Non-bool while condition";

            // Type mismatch diagnostics
            case DiagnosticKind::T0020_ReturnTypeMismatch:
                return "Return type mismatch";
            case DiagnosticKind::T0021_AssignmentTypeMismatch:
                return "Assignment type mismatch";
            case DiagnosticKind::T0022_ExplicitConstantTypeMismatch:
                return "Explicit constant type mismatch";
            case DiagnosticKind::T0023_ExplicitVariableTypeMismatch:
                return "Explicit variable type mismatch";
            case DiagnosticKind::T0024_TypeFieldInitializerMismatch:
                return "Type field initializer mismatch";
            case DiagnosticKind::T0025_ArithmeticOperandTypeMismatch:
                return "Arithmetic operand type mismatch";
            case DiagnosticKind::T0026_ComparisonOperandTypeMismatch:
                return "Comparison operand type mismatch";
            case DiagnosticKind::T0027_EnumFieldValueTypeMismatch:
                return "Enum field value type mismatch";
            case DiagnosticKind::T0078_ArrayElementTypeMismatch:
                return "Array element type mismatch";
            case DiagnosticKind::T0080_ArrayLengthMismatch:
                return "Array length mismatch";

            // Declaration-shape and reference diagnostics
            case DiagnosticKind::T0028_NonExternVariadicFunction:
                return "Non-extern variadic function";
            case DiagnosticKind::T0029_FlagEnumExplicitValue:
                return "Flag enum explicit value";
            case DiagnosticKind::T0030_TypeDotNewDeclaration:
                return "Type.new declaration not allowed";
            case DiagnosticKind::T0031_ReferenceOrSliceReturn:
                return "Functions cannot return references or slices";
            case DiagnosticKind::T0032_AlreadyReference:
                return "Already a reference";

            // Duplicate declaration diagnostics
            case DiagnosticKind::T0033_DuplicateDeclaration:
                return "Duplicate declaration";
            case DiagnosticKind::T0034_DuplicateConstantDeclaration:
                return "Duplicate constant declaration";
            case DiagnosticKind::T0035_DuplicateVariableDeclaration:
                return "Duplicate variable declaration";
            case DiagnosticKind::T0036_DuplicateParameterDeclaration:
                return "Duplicate parameter declaration";
            case DiagnosticKind::T0037_DuplicateEnumFieldDeclaration:
                return "Duplicate enum field declaration";
            case DiagnosticKind::T0038_DuplicateTypeFieldDeclaration:
                return "Duplicate type field declaration";
            case DiagnosticKind::T0039_DuplicateFunctionDeclaration:
                return "Duplicate function declaration";
            case DiagnosticKind::T0040_DuplicateTypeDeclaration:
                return "Duplicate type declaration";
            case DiagnosticKind::T0041_NumberLiteralOutOfRange:
                return "Number literal out of range";
            case DiagnosticKind::T0079_EmptyArrayLiteral:
                return "Empty arrays are not allowed";
            case DiagnosticKind::T0042_UnusedLocalVariable:
                return "Unused local variable";
            case DiagnosticKind::T0043_UnusedLocalConstant:
                return "Unused local constant";
            case DiagnosticKind::T0044_UnusedParameter:
                return "Unused parameter";
            case DiagnosticKind::T0045_UnexpectedAnnotationArgument:
                return "Unexpected annotation argument";
            case DiagnosticKind::T0046_DuplicateAnnotationArgument:
                return "Duplicate annotation argument";
            case DiagnosticKind::T0047_ExternMethodRequiresSymbol:
                return "Extern method requires a symbol";
            case DiagnosticKind::T0048_UnknownArgumentName:
                return "Unknown argument name";
            case DiagnosticKind::T0049_DuplicateArgumentBinding:
                return "Duplicate argument";
            case DiagnosticKind::T0050_MissingRequiredArgument:
                return "Missing required argument";
            case DiagnosticKind::T0051_DefaultParameterTypeMismatch:
                return "Default parameter type mismatch";
            case DiagnosticKind::T0052_NonTrailingDefaultParameter:
                return "Non-trailing default parameter";
            case DiagnosticKind::T0053_AssignmentToInitConstant:
                return "Assignment to init constant";
            case DiagnosticKind::T0054_InitConstantAlreadyInitialized:
                return "Init constant already initialized";
            case DiagnosticKind::T0055_UninitializedInitConstant:
                return "Uninitialized init constant";
            case DiagnosticKind::T0056_NonGlobalInitConstant:
                return "Non-global init constant";
            case DiagnosticKind::T0100_InitReferenceWithoutRef:
                return "Reference init constant without 'ref'";
            case DiagnosticKind::T0057_AssignmentToConstant:
                return "Assignment to constant";
            case DiagnosticKind::T0087_MutatingMethodOnConstant:
                return "Mutating method call on constant";
            case DiagnosticKind::T0088_MutatingMethodThroughConstantReference:
                return "Mutating method call on reference to constant";
            case DiagnosticKind::T0058_AssignmentThroughConstantReference:
                return "Assignment through reference to constant";
            case DiagnosticKind::T0059_UnreachableCode:
                return "Unreachable code";
            case DiagnosticKind::T0060_MissingReturn:
                return "Not all paths return a value";
            case DiagnosticKind::T0061_DivisionByZero:
                return "Division by zero";
            case DiagnosticKind::T0062_ConstantOverflow:
                return "Constant expression overflows its type";
            case DiagnosticKind::T0063_UnaryOperandTypeMismatch:
                return "Unary operator cannot be applied to this type";
            case DiagnosticKind::T0064_VoidParameterType:
                return "Parameter cannot have type 'void'";
            case DiagnosticKind::T0065_BinaryOperandTypeMismatch:
                return "Binary operator cannot be applied to this type";
            case DiagnosticKind::T0066_NotABuiltinType:
                return "Not a builtin type";
            case DiagnosticKind::T0067_DuplicateBuiltinTypeBinding:
                return "Builtin type is already bound";
            case DiagnosticKind::T0068_BuiltinTypeFieldIgnored:
                return "Field in builtin type is ignored";
            case DiagnosticKind::T0069_BuiltinTypeConstructorIgnored:
                return "Constructor in builtin type is ignored";
            case DiagnosticKind::T0070_BuiltinMethodBodyIgnored:
                return "Method body in builtin type is ignored";
            case DiagnosticKind::T0071_UnknownBuiltinMethodIgnored:
                return "Unknown method in builtin type is ignored";
            case DiagnosticKind::T0072_InvalidOperatorMethodSignature:
                return "Invalid operator method signature";
            case DiagnosticKind::T0083_BitwiseMethodOnNonIntegerType:
                return "Bitwise methods are only available on integer types";
            case DiagnosticKind::T0089_UnsupportedFloatBits:
                return "Unsupported bit count for floats";
            case DiagnosticKind::T0073_MethodCallMissingDot:
                return "Method call is missing the leading dot";
            case DiagnosticKind::T0074_StaticMethodTypeNameMismatch:
                return "Static method type name does not match the enclosing type";
            case DiagnosticKind::T0081_CallReturnsNoValue:
                return "Cannot store the result of a call that returns nothing";
            case DiagnosticKind::T0082_GlobalConstantWithCall:
                return "Global constant initializers cannot call functions";
            case DiagnosticKind::T0084_GlobalConstantNotComputable:
                return "Global constant initializers must be computable at compile time";
            case DiagnosticKind::T0085_ReferenceOrSliceField:
                return "Fields cannot store references or slices";
            case DiagnosticKind::T0086_ReferenceOrSliceArrayElement:
                return "Arrays cannot store references or slices";
            case DiagnosticKind::T0090_MethodRequiresDynamicArray:
                return "Only dynamic arrays can add or remove elements";
            case DiagnosticKind::T0091_DynamicArrayElement:
                return "Arrays cannot store dynamic arrays";
            case DiagnosticKind::T0092_ReferenceToNonVariable:
                return "References can only be taken to variables";
            case DiagnosticKind::T0093_MemberAccessInStaticMethod:
                return "Member access is not allowed in static methods";
            case DiagnosticKind::T0094_PrivateFieldAccessOutsideType:
                return "Private fields cannot be accessed from outside their type";
            case DiagnosticKind::T0095_MutatingMethodOnImmutableSlice:
                return "Immutable slices cannot be mutated";
            case DiagnosticKind::T0096_FieldAccessMissingDot:
                return "Field access is missing the leading dot";
            case DiagnosticKind::T0075_DuplicateMethodDeclaration:
                return "Duplicate method declaration";
            case DiagnosticKind::T0076_MemberAccessInDefaultParameter:
                return "Member access is not allowed for default parameters";
            case DiagnosticKind::T0077_PrivateMethodCallOutsideType:
                return "Private methods cannot be called from outside their type";
            case DiagnosticKind::T0097_PreludeTypeMissing:
                return "Prelude does not declare a type the compiler needs";
            case DiagnosticKind::T0098_PreludeCFunctionMissing:
                return "Prelude does not declare a C function the compiler needs";
            case DiagnosticKind::T0099_PreludeCFunctionMismatch:
                return "Prelude declares a C function differently than the compiler needs";
            case DiagnosticKind::Unknown:
            default:
                return "Unknown diagnostic";
        }
    }

    Diagnostic::Diagnostic(
        DiagnosticLevel level,
        DiagnosticKind kind,
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        std::optional<std::string> fix)
        : m_level(level)
        , m_kind(kind)
        , m_message(DiagnosticMessage(kind))
        , m_source(source)
        , m_location(location)
        , m_fix(std::move(fix))
    {
    }

    Diagnostic::Diagnostic(
        DiagnosticLevel level,
        DiagnosticKind kind,
        std::optional<std::string> fix)
        : m_level(level)
        , m_kind(kind)
        , m_message(DiagnosticMessage(kind))
        , m_fix(std::move(fix))
    {
    }

    std::string_view Diagnostic::code() const noexcept
    {
        return DiagnosticCode(m_kind);
    }

    void Diagnostic::addPrimaryLabel(const SourceLocation& location, std::string text)
    {
        m_labels.push_back(Label{
            .source = m_source,
            .location = location,
            .text = std::move(text),
            .isPrimary = true
            });
    }

    void Diagnostic::addSecondaryLabel(const SourceLocation& location, std::string text)
    {
        m_labels.push_back(Label{
            .source = m_source,
            .location = location,
            .text = std::move(text),
            .isPrimary = false
            });
    }

    void Diagnostic::addRelatedPrimaryLabel(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        std::string message,
        std::string text)
    {
        auto relatedSource = source;
        if (!relatedSource)
        {
            relatedSource = m_source;
        }

        auto related = RelatedReport{
            .source = relatedSource,
            .message = std::move(message),
            .labels = {}
        };
        related.labels.push_back(Label{
            .source = relatedSource,
            .location = location,
            .text = std::move(text),
            .isPrimary = true,
        });
        m_related.push_back(std::move(related));
    }
}
