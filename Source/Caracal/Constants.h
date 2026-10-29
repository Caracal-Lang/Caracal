#pragma once

namespace Caracal
{
    // name of the implicit this parameter for methods, which is not visible in 
    // Caracal source code but is used in the semantic context and IR generation
    // the leading dot cant appear in a Caracal identifier
    inline constexpr const char* ImplicitThisName = ".this";
    inline constexpr const char* GlobalInitializerName = ".global_init";

    inline constexpr const char* EntryPointFunctionName = "caracalMain";
    inline constexpr const char* UserMainFunctionName = "main";
    inline constexpr const char* CRuntimeEntrySymbolName = "main";
    inline constexpr const char* UserMainSymbolName = "caracal.userMain";

    // free functions and constants the compiler owns, like __caracal_project_name
    inline constexpr const char* CompilerNamePrefix = "__caracal_";

    // the language-wide member name for array lengths
    inline constexpr const char* ArrayLengthMemberName = "length";

    // the bitwise intrinsic method names on the integer builtin types
    inline constexpr const char* BuiltinBitAndMethodName = "bitAnd";
    inline constexpr const char* BuiltinBitOrMethodName = "bitOr";
    inline constexpr const char* BuiltinBitXorMethodName = "bitXor";
    inline constexpr const char* BuiltinBitNotMethodName = "bitNot";
    inline constexpr const char* BuiltinShiftLeftMethodName = "shiftLeft";
    inline constexpr const char* BuiltinShiftRightMethodName = "shiftRight";

    // the operator method names on builtin types
    inline constexpr const char* BuiltinAddMethodName = "add";
    inline constexpr const char* BuiltinSubtractMethodName = "subtract";
    inline constexpr const char* BuiltinMultiplyMethodName = "multiply";
    inline constexpr const char* BuiltinDivideMethodName = "divide";
    inline constexpr const char* BuiltinEqualsMethodName = "equals";
    inline constexpr const char* BuiltinNotEqualsMethodName = "notEquals";
    inline constexpr const char* BuiltinLessThanMethodName = "lessThan";
    inline constexpr const char* BuiltinLessOrEqualMethodName = "lessOrEqual";
    inline constexpr const char* BuiltinGreaterThanMethodName = "greaterThan";
    inline constexpr const char* BuiltinGreaterOrEqualMethodName = "greaterOrEqual";
    inline constexpr const char* BuiltinLogicalAndMethodName = "logicalAnd";
    inline constexpr const char* BuiltinLogicalOrMethodName = "logicalOr";
    inline constexpr const char* BuiltinNegateMethodName = "negate";
    inline constexpr const char* BuiltinLogicalNegateMethodName = "logicalNegate";

    // annotation names and their named arguments
    inline constexpr const char* ExternAnnotationName = "extern";
    inline constexpr const char* FlagAnnotationName = "flag";
    inline constexpr const char* StepAnnotationName = "step";
    inline constexpr const char* BuiltinAnnotationName = "builtin";
    inline constexpr const char* SymbolAnnotationArgumentName = "symbol";
    inline constexpr const char* KindAnnotationArgumentName = "kind";
    inline constexpr const char* BitsAnnotationArgumentName = "bits";
    inline constexpr const char* SignedAnnotationArgumentName = "signed";
    inline constexpr const char* BuiltinKindIntName = "int";
    inline constexpr const char* BuiltinKindFloatName = "float";
    inline constexpr const char* BuiltinKindBoolName = "bool";
    inline constexpr const char* BuiltinKindPointerName = "pointer";
}
