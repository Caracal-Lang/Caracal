#pragma once

#include <Caracal/CompilationContext.h>
#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/Semantic/SemanticContext.h>

namespace Caracal
{
    void validateBuiltinMethod(
        const CompilationContext& context,
        const CompilationUnit& unit,
        u32 methodIndex,
        TypeDefinition& typeDefinition,
        Type typeType,
        SemanticContext& module,
        DiagnosticsBag& diagnostics);

    void registerOperatorMethod(
        const CompilationContext& context,
        const CompilationUnit& unit,
        u32 methodIndex,
        TypeDefinition& typeDefinition,
        Type typeType,
        SemanticContext& module);
}
