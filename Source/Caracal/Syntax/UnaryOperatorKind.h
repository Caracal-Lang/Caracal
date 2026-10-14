#pragma once

#include <Caracal/API.h>

#include <string>

namespace Caracal
{
    enum class UnaryOperatorKind
    {
        Invalid,
        LogicalNegation,
        ValueNegation,
        ReferenceOf
    };

    [[nodiscard]] CARACAL_API std::string stringify(UnaryOperatorKind kind);
}
