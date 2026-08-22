#pragma once

#include <Caracal/API.h>
#include <Caracal/Semantic/SemanticContext.h>
#include <Caracal/Syntax/NodeBuffer.h>
#include <Caracal/Syntax/TokenBuffer.h>

#include <string>

namespace Caracal
{
    [[nodiscard]] CARACAL_API std::string prettyPrint(const NodeBuffer& buffer, const TokenBuffer& tokens, SemanticContext* module);
}
