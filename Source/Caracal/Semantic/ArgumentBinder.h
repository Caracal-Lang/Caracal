#pragma once

#include <Caracal/CompilationContext.h>
#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/Semantic/Parameter.h>

#include <span>
#include <string>
#include <vector>

namespace Caracal
{
    inline constexpr u32 UnboundArgumentSlot = 0xFFFFFFFF;
    inline constexpr u32 DefaultBoundArgumentSlot = 0xFFFFFFFE;

    struct ArgumentBinding
    {
        std::vector<u32> variadic;
        bool ok = true;
    };

    [[nodiscard]] ArgumentBinding bindCallArguments(
        const CompilationContext& context,
        const CompilationUnit& unit,
        u32 callIndex,
        std::span<const u32> argumentNodes,
        const std::vector<Parameter>& parameters,
        size_t parameterOffset,
        bool isVariadic,
        const std::string& functionName,
        DiagnosticsBag& diagnostics,
        std::vector<u32>& orderedStack,
        size_t orderedStart);
}
