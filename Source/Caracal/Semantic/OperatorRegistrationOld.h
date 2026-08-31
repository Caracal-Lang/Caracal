#pragma once

#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/Semantic/SemanticContext.h>
#include <Caracal/Syntax/MethodDefinitionStatement.h>
#include <Caracal/Syntax/TokenBuffer.h>

namespace Caracal
{
    // the "method name maps to operator surface" subsystem: builtin operator stubs lower to instructions,
    // non-builtin static equals/notEquals with the builtin shape register as == and != with a callee
    void validateBuiltinMethodOld(const MethodDefinitionStatement* methodStatement, TypeDefinition& typeDefinition, Type typeType, const TokenBuffer& tokens, SemanticContext& module, DiagnosticsBag& diagnostics);
    void registerOperatorMethodOld(const MethodDefinitionStatement* methodStatement, TypeDefinition& typeDefinition, Type typeType, SemanticContext& module);
}
