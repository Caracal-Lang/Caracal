#pragma once

#include <Caracal/Semantic/SemanticContext.h>
#include <Caracal/Syntax/FunctionDefinitionStatement.h>
#include <Caracal/Syntax/TypeDefinitionStatement.h>

#include <vector>

namespace Caracal
{
    // look for slice parameters that can be promoted to immutable to allow for both kinds as function arguments
    void promoteReadOnlySliceParametersOld(
        const std::vector<const FunctionDefinitionStatement*>& functionDeclarations,
        const std::vector<const TypeDefinitionStatement*>& typeDeclarations,
        SemanticContext& module);
}
