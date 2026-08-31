#pragma once

#include <Caracal/CompilationContext.h>
#include <Caracal/Semantic/SemanticContext.h>
#include <Caracal/Syntax/NodeIndex.h>

#include <vector>

namespace Caracal
{
    // look for slice parameters that can be promoted to immutable to allow for both kinds as function arguments
    void promoteReadOnlySliceParameters(
        const CompilationContext& context,
        SemanticContext& module,
        const std::vector<NodeIndex>& functionDeclarations,
        const std::vector<NodeIndex>& typeDeclarations);
}
