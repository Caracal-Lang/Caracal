#pragma once

#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/Semantic/Parameter.h>
#include <Caracal/Syntax/TokenBuffer.h>

#include <string>
#include <vector>

namespace Caracal
{
    class FunctionCallExpression;
    class Expression;

    struct ArgumentBindingResult
    {
        std::vector<const Expression*> ordered;
        std::vector<const Expression*> variadic;
        bool ok = true;
    };

    ArgumentBindingResult bindCallArgumentsOld(
        const std::vector<Parameter>& parameters,
        size_t parameterOffset,
        bool isVariadic,
        const FunctionCallExpression& call,
        const std::string& functionName,
        DiagnosticsBag& diagnostics,
        const TokenBuffer& tokens);
}
