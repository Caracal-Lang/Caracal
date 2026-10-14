#pragma once

#include <Caracal/Semantic/SemanticContext.h>
#include <Caracal/Syntax/BinaryOperatorKind.h>

#include <string>

namespace Caracal
{
    [[nodiscard]] std::string formatTypeName(SemanticContext& module, Type type);
    [[nodiscard]] std::string formatBinaryOperator(BinaryOperatorKind binaryOperator);
}
