#pragma once

#include <Caracal/API.h>

#include <string>

namespace Caracal
{
    enum class BinaryOperatorKind
    {
        Invalid,
        ConstructorCall,
        MethodCall,
        MemberAccess,
        AdditionWrapping,
        SubtractionWrapping,
        MultiplicationWrapping,
        Division,
        Equal,
        NotEqual,
        LessThan,
        LessOrEqual,
        GreaterThan,
        GreaterOrEqual,
        LogicalAnd,
        LogicalOr,
    };

    [[nodiscard]] CARACAL_API std::string stringify(BinaryOperatorKind kind);
}
